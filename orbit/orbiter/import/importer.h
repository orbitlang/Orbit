// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_IMPORT_IMPORTER_H_
#define ORBIT_ORBITER_IMPORT_IMPORTER_H_

#include <orbit/orbiter/datatype/hashmap.h>
#include <orbit/orbiter/datatype/list.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

#include <orbit/orbiter/import/locator.h>
#include <orbit/orbiter/import/registry.h>

namespace orbiter {
    class Fiber;
}

namespace orbiter::import {
    constexpr const char *kExtension[] = {
        ".orb",

        // The last extension MUST BE the one that indicates a
        // dynamic library in the operating system in use.
#if defined(_ORBIT_PLATFORM_DARWIN)
        ".dylib"
#elif defined(_ORBIT_PLATFORM_WINDOWS)
        ".dll"
#else
        ".so"
#endif
    };

    /// Orbit's logical path separator — used inside canonical import keys.
    /// Always `/`, regardless of platform.
    constexpr auto *kPathSep = "/";

    /// Host filesystem path separator.
#if defined(_ORBIT_PLATFORM_WINDOWS)
    constexpr auto *kHostPathSep = "\\";
#else
    constexpr auto *kHostPathSep = "/";
#endif

    /// Module registry hashmap: canonical key → ModuleEntry.
    using ModuleMap = HashMap<ORString *,
        ModuleEntry *,
        ORStringEqual,
        ORStringHash>;

    /// Pointer equality for `Fiber*` keys in the wait-for graph.
    inline bool FiberPtrEqual(const Fiber *a, const Fiber *b) noexcept {
        return a == b;
    }

    /// Hash for `Fiber*` keys in the wait-for graph.
    inline size_t FiberPtrHash(const Fiber *f) noexcept {
        return reinterpret_cast<size_t>(f);
    }

    /// Wait-for graph: `fiber → entry it is currently blocked on`.
    /// Used to detect cross-fiber circular imports before enqueueing a new waiter.
    using WaitForMap = HashMap<
        Fiber *,
        ModuleEntry *,
        FiberPtrEqual,
        FiberPtrHash
    >;

    enum class AcquireOutcome : U8 {
        /// `entry->module` is the final, fully-loaded module — use it.
        LOADED,
        /// `entry->module` is the partial module — use it pragmatically.
        /// Two cases collapse here: a literal same-fiber
        /// self-import, and a cross-fiber cycle the calling fiber would
        /// close (it takes the partial instead of blocking, which is what
        /// breaks the deadlock).
        PARTIAL,
        /// Fresh entry just inserted; the calling fiber owns the load and
        /// must run the loader, then `Commit`/`Fail`.
        FRESH,
        /// Cross-fiber LOADING with no cycle: the calling fiber has been
        /// enqueued on the peer's waiters and must SUSPEND until woken.
        /// The caller re-runs `Import` on wake.
        BLOCKED,
        /// Allocation failure; isolate panic set.
        ERROR,
    };

    enum class ImportStatus : U8 {
        OK,
        BLOCKED,
        ERROR
    };

    class Importer {
        Isolate *isolate_;

        /// Ordered list of root paths (ORString elements). Append = lowest
        /// precedence. Held as a strong handle for the Importer's lifetime.
        HList roots_;

        /// Cache of canonical key → ModuleEntry. Stores both loaded and
        /// in-progress (LOADING) modules.
        ModuleMap modules_;

        /// Wait-for graph: `fiber → entry it is blocked on`. Used to spot
        /// cross-fiber circular imports before enqueueing a new waiter.
        WaitForMap wait_for_;

        /// Brief lock around `modules_` and `wait_for_`. Held only for
        /// cache and graph operations — **never** during a module's
        /// top-level execution.
        sync::AsyncRWLock cache_lock_;

        AcquireOutcome Acquire(ORString *key, ModuleEntry * &out) noexcept;

        AcquireOutcome EnqueueAndWait(Fiber *me, ModuleEntry *entry) noexcept;

        /**
         * @brief Hand a freshly-loaded SOURCE entry over to its executor and
         *        block the importing fiber on it.
         *
         * Called by the SOURCE path after the module has been compiled and
         * `Prepare`'d: @p executor is the fiber that will run the module's
         * top-level (and is therefore the entry's `owner`, set here — not at
         * `ModuleEntryNew` time, since the executor does not exist yet then).
         * The importing fiber (`Fiber::Current()`) is enqueued as a waiter,
         * so it is woken when @p executor `Commit`/`Fail`s the entry.
         *
         * Takes the cache unique lock.
         *
         * @return false on allocation failure (wait-for edge could not be
         *         registered); the caller then `Fail`s the entry.
         */
        bool BlockOnExecutor(ModuleEntry *entry, Fiber *executor) noexcept;

        /**
         * @brief Detect cross-fiber circular imports.
         * 
         * Walk the wait-for graph from @p target back through its owner
         * chain to detect whether enqueueing @p me as a waiter would
         * close a cycle. The caller must hold the cache unique lock.
        */
        [[nodiscard]] bool HasCycle(const Fiber *me, const ModuleEntry *target) const;

        HModule LoadScriptSource(ORString *key, const Descriptor &desc, HCode &code, ModuleEntry *entry);

        /**
         * @brief Walk the locator chain for @p key.
         * 
         * Hardcoded order, tri-state: builtin → fs-source. Stops at the first
         * locator that does not return NOT_MINE; if every locator declines,
         * sets `ImportError` on the isolate panic (with the list of locators
         * that were tried) and returns NOT_MINE.
         * 
         * @param key  The canonical, absolute import key.
         * @param out  Filled only when the result is FOUND.
         * 
         * @return The tri-state outcome. On ERROR the isolate panic is set.
         */
        LocateResult Resolve(const ORString *key, Descriptor *out) const;

        /**
         * @brief Acquire the cache entry for @p key, creating it if missing.
         *
         * Takes the cache unique lock and re-checks under the lock before
         * allocating: this closes the race where two fibers both miss in a
         * prior `Lookup` and would otherwise double-insert (the underlying
         * `HashMap::Insert` doesn't dedup keys, so a duplicate would corrupt
         * the cache). On a hit under the lock, the existing entry is
         * returned untouched.
         *
         * @param key            Canonical, absolute import key.
         *
         * @return The entry (fresh or pre-existing), or nullptr on
         *         allocation failure (isolate panic set).
         */
        ModuleEntry *Insert(ORString *key);

        /**
         * @brief Look up a cache entry by canonical key.
         *
         * Acquires the cache shared lock for the duration of the lookup.
         * Returns the entry verbatim — including LOADING ones — so the
         * caller can distinguish "miss" (nullptr) from "in progress"
         * (state == LOADING).
         *
         * @param key  Canonical, absolute import key.
         *
         * @return The entry, or nullptr on miss (no panic set).
         */
        ModuleEntry *Lookup(ORString *key);

        /**
         * @brief Attach @p module and @p spec to a LOADING entry.
         *
         * Called once by the loader **before** running the module's top-level,
         * so that re-entrant imports from the same fiber observe the
         * partial module and `__spec__` is available throughout loading.
         * The module object itself is populated in place as the top-level runs;
         * the pointer set here is the same one that `Commit` will eventually mark as LOADED.
         *
         * It is a programming error to call Prepare on an entry that is
         * not LOADING, or on one that already has `module`/`spec` set —
         * both caught by assertion.
         *
         * @param entry   The LOADING entry obtained from a prior `Insert`.
         * @param module  The (typically empty) module to attach. INCREF'd.
         * @param spec    The public `ImportSpec`. INCREF'd.
         */
        void Prepare(ModuleEntry *entry, Module *module, ImportSpec *spec);

        void PrepareCommit(ModuleEntry *entry, Module *module, ImportSpec *spec);

    public:
        explicit Importer(Isolate *isolate) : isolate_(isolate), modules_(isolate), wait_for_(isolate) {
        }

        ~Importer();

        /**
         * @brief Allocate the backing structures. Call once before use.
         *
         * @return false on allocation failure (isolate panic set).
         */
        bool Initialize();

        /**
         * @brief Append a search root from a C string (lowest precedence).
         *
         * @param path  A NUL-terminated path. Expected absolute and already
         *              normalized to unix-style separators — this entry point
         *              does not canonicalize.
         *
         * @return false on allocation failure (isolate panic set).
         */
        bool AddRoot(const char *path) const;

        /// Append an already-built root string (lowest precedence).
        bool AddRoot(ORString *path) const;

        [[nodiscard]] ImportStatus Import(ORString *raw, const ImportSpec *origin, Module * &out_module) noexcept;

        [[nodiscard]] Isolate *GetIsolate() const noexcept {
            return this->isolate_;
        }

        [[nodiscard]] List *Roots() const {
            return this->roots_.get();
        }

        /**
          * @brief Mark @p entry as LOADED.
          *
          * Transitions a LOADING entry to LOADED under the cache unique
          * lock. `module` and `spec` are *not* attached here — they were
          * set by `Prepare` before the top-level ran; this call simply
          * publishes the entry as fully initialized so subsequent `Lookup`s
          * see LOADED.
          *
          * It is a programming error to call Commit on an entry that is not
          * LOADING — caught by assertion.
          *
          * @param entry  The LOADING entry obtained from a prior `Insert`.
          */
        void Commit(ModuleEntry *entry);

        /**
         * @brief Drop a failed entry, allowing a future retry.
         *
         * Removes @p entry from the cache under the unique lock and
         * releases it (DECREF of every held Orbit reference, free). Used
         * when a module's top-level raises: per README "FAILED → removal
         * + retry consentito", a poisoned half-initialized entry must
         * never linger.
         *
         * The pointer is **consumed**: do not reference it after Fail returns.
         *
         * @param entry  The LOADING entry obtained from a prior `Insert`.
        */
        void Fail(ModuleEntry *entry);
    };

    /**
     * @brief Canonicalize a raw import string into the registry's cache key.
     *
     * The result is **always absolute and OS-independent**, even when the
     * input was relative — this is what guarantees a module reached by two
     * different spellings hashes to the same entry.
     *
     * Rules (see `import/README.md`):
     *   - Empty input → `ImportError(INVALID_KEY)`.
     *   - `::`-prefixed: opaque builtin scheme; only `[A-Za-z0-9_:]` allowed;
     *     returned verbatim.
     *   - Otherwise filesystem-style: `\` → `/`, collapse `//`, collapse `.`
     *     segments, reject any `..` segment with `INVALID_KEY` (imports are
     *     not disk paths; no upward traversal).
     *   - A leading `./` is *relative*: resolved against `dirname(origin->name)`,
     *     then folded into an absolute key. Without an @p origin this yields
     *     `ImportError(NO_ORIGIN)`; when @p origin is not a SOURCE module
     *     (e.g. a builtin), `ImportError(INVALID_ORIGIN)` — relative imports
     *     only have meaning for disk-loaded modules.
     *
     * @param isolate  Owning isolate.
     * @param raw      The raw import string from source.
     * @param origin   ImportSpec of the importing module, or nullptr for
     *                 top-level imports (no relative form allowed in that
     *                 case).
     *
     * @return The canonical key, or an empty handle on error (isolate panic
     *         set with one of the `ImportError` reasons).
     */
    HORString Canonicalize(Isolate *isolate, ORString *raw, const ImportSpec *origin);

    /**
     * @brief Top-level import: produce the module for @p raw.
     *
     * Pipeline (see `import/README.md`):
     *   Canonicalize → `Acquire` → (FRESH ⇒ Resolve + loader + Prepare/Commit)
     *
     * Cross-fiber: on contention with another fiber already loading the
     * same key, `Acquire` enqueues the current fiber on the peer's
     * waiters and `Import` returns `BLOCKED`. The caller (opcode handler)
     * must transition the fiber to SUSPENDED and re-run the same `Import`
     * call when the fiber wakes — the registry guarantees a wake on the
     * peer's `Commit` / `Fail`.
     *
     * @param isolate     Owning isolate.
     * @param raw         The raw import string from source.
     * @param origin      ImportSpec of the importing module, or nullptr
     *                    for top-level imports.
     * @param out_module  Receives the loaded module on `ImportStatus::OK`;
     *                    untouched on BLOCKED / ERROR.
     *
     * Import cycles do not error: a fiber that would close a cycle takes
     * the peer's partial module instead of blocking, which
     * is what breaks the would-be deadlock.
     *
     * @return Tri-state outcome. On ERROR the isolate panic is set
     *         (module not found, locator error, or allocation failure).
     */
    ImportStatus Import(const Isolate *isolate, ORString *raw, const ImportSpec *origin, Module * &out_module);

    ImportStatus Import(const Isolate *isolate, ORString *raw, const Module *base, Module * &out_module);
}

#endif // !ORBIT_ORBITER_IMPORT_IMPORTER_H_
