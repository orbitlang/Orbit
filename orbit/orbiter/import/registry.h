// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_IMPORT_REGISTRY_H_
#define ORBIT_ORBITER_IMPORT_REGISTRY_H_

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/fqueue.h>

#include <orbit/orbiter/import/importspec.h>

namespace orbiter {
    class Fiber;
}

namespace orbiter::import {
    using namespace orbiter::datatype;

    /**
     * @brief Lifecycle state of a `ModuleEntry`.
     *
     * LOADING — the entry exists, the module's top-level is in progress.
     *           `module` is already populated (even if only partially built)
     *           so that re-entrant imports from the same fiber observe the
     *           partial module. `spec` is null until the loader commits.
     * LOADED  — top-level completed successfully. `module` and `spec` are the
     *           final, immutable values; subsequent imports return `module`.
     * FAILED  — top-level raised. The entry is removed from the cache so that
     *           a later import may retry from scratch.
     */
    enum class ModuleState : U8 {
        LOADING,
        LOADED,
        FAILED,
    };

    /**
     * @brief Per-module cache entry — purely engine-internal.
     *
     * Not an Orbit type: this is a plain C++ struct allocated through the
     * isolate allocator, never exposed to user code. References to Orbit
     * objects (`name`, `module`, `spec`) are kept alive via manual
     * `O_FAST_INCREF`/`O_FAST_DECREF` — taken in `ModuleEntryNew` /
     * mutation helpers, released in `ModuleEntryDel`.
     *
     * The entry is the value stored against each canonical key in the
     * registry's hashmap. It exists from the moment a load begins
     * (`Importer::Insert`) until the module is committed
     * (`Commit`) or removed on failure (`Fail`).
     *
     * Fields:
     *   - `name`    canonical key (also the hashmap's key); `INCREF`'d on
     *               construction.
     *   - `module`  module being built (already in LOADING for partial
     *               visibility during same-fiber cycles) or final (LOADED);
     *               `INCREF`'d when set.
     *   - `spec`    public `ImportSpec` attached on `Commit`; nullptr while
     *               LOADING; `INCREF`'d when set.
     *   - `state`   lifecycle marker; transitions LOADING → LOADED/FAILED.
     */
    struct ModuleEntry {
        ORString *name = nullptr;

        Module *module = nullptr;

        ImportSpec *spec = nullptr;

        ModuleState state = ModuleState::LOADING;

        /// Fiber that owns this LOADING entry. Used to tell same-fiber
        /// cyclic imports (return partial module) apart from cross-fiber
        /// concurrent loads (enqueue on `waiters` and block). Set at
        /// `ModuleEntryNew` time; left untouched after that.
        Fiber *owner = nullptr;

        /// Fibers blocked waiting for this entry to leave LOADING. Drained
        /// on `Commit`/`Fail`, with each woken fiber re-scheduled via
        /// `Orbiter::PushFiber`. Must be empty by the time the entry is
        /// destroyed (the `FiberQueue` destructor asserts).
        FiberQueue<false> waiters;
    };

    /**
     * @brief Allocate a new `ModuleEntry` in `LOADING` state.
     *
     * Memory comes from the isolate allocator. A strong reference is taken
     * on @p name; `module` and `spec` are left null (the loader populates
     * `module` as the top-level runs; the orchestrator hands `spec` over on
     * `Commit`).
     *
     * @param isolate  Owning isolate.
     * @param name     Canonical key for the new entry. `INCREF`'d.
     *
     * @return The new entry, or nullptr on allocation failure (isolate
     *         panic set).
     */
    ModuleEntry *ModuleEntryNew(Isolate *isolate, ORString *name);

    /**
     * @brief Deletes a `ModuleEntry` and frees associated resources.
     *
     * This function ensures that the given `ModuleEntry` is safely removed
     * by decrementing reference counts for its name, module, and spec fields.
     * After releasing these fields, it deallocates the memory associated
     * with the entry using the specified isolate's memory allocator.
     *
     * The function is null-safe, meaning it performs no operation if the
     * provided `entry` is a null pointer.
     *
     * @param isolate The isolate instance whose allocator is used to free memory.
     * @param entry The `ModuleEntry` to be deleted. If null, the function returns immediately.
     */
    void ModuleEntryDel(Isolate *isolate, ModuleEntry *entry);
}

#endif // !ORBIT_ORBITER_IMPORT_REGISTRY_H_
