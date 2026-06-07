// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_IMPORT_IMPORTER_H_
#define ORBIT_ORBITER_IMPORT_IMPORTER_H_

#include <orbit/orbiter/datatype/hashmap.h>
#include <orbit/orbiter/datatype/list.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

#include <orbit/orbiter/import/locator.h>
#include <orbit/orbiter/import/module_entry.h>

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

    /// Host filesystem path separator — used when building paths to hand
    /// to the OS (stat, open, …). `\` on Windows, `/` everywhere else.
    constexpr auto *kHostPathSep = _ORBIT_PLATFORM_PATHSEP;

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

    /// Top-level outcome of `Import`. Mirrors the Channel send/recv idiom:
    /// the caller (opcode handler) translates BLOCKED into a fiber SUSPEND
    /// and re-runs the same `Import` call on wake.
    enum class ImportStatus : U8 {
        /// Module available in `out_module`.
        OK,
        /// Calling fiber enqueued on a peer's waiters; caller must SUSPEND
        /// and retry on wake. `out_module` left untouched.
        BLOCKED,
        /// Isolate panic set; caller propagates. `out_module` left untouched.
        ERROR,
    };

    /**
     * @brief Per-isolate import subsystem.
     *
     * See `import/README.md` for the full design.
     */
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

        /**
         * @brief Single-shot "get a usable entry for @p key" operation —
         *        the heart of `Import`.
         *
         * Performs lookup, state inspection, cycle detection, and (when
         * needed) insert / waiter enqueue under a single lock. Subsumes
         * what `Lookup` + `Insert` would do separately, avoiding the race
         * windows between them.
         *
         * The caller (`Import`) must hold `cache_lock_` in unique mode for
         * the whole call — this method is lock-free internally.
         *
         * Outcomes (see `AcquireOutcome` for full semantics):
         *   - LOADED  / PARTIAL : `out->module` is what to hand back.
         *   - FRESH             : `out` is a brand-new LOADING entry; the
         *                         calling fiber must drive the load.
         *   - BLOCKED           : current fiber enqueued on a peer's
         *                         waiters; caller must SUSPEND.
         *   - ERROR             : allocation failure; panic set.
         *
         * @param key  Canonical, absolute import key.
         * @param out  Receives the entry for LOADED / PARTIAL / FRESH;
         *             left untouched on BLOCKED / ERROR.
         */
        AcquireOutcome Acquire(ORString *key, ModuleEntry * &out) noexcept;

        /**
         * @brief Enqueue @p me as a waiter on @p entry and register the
         *        matching wait-for edge.
         *
         * Internal helper shared by the cross-fiber-LOADING path of
         * `Acquire` and by `BlockOnExecutor`. Assumes the caller holds
         * `cache_lock_` in unique mode.
         *
         * @return BLOCKED on success, ERROR if the wait-for edge could not
         *         be allocated (the panic is left to the caller — typical
         *         OOM, isolate is already in trouble).
         */
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

        /**
         * @brief Compile a SOURCE module from disk and Prepare its entry.
         *
         * Opens `desc.origin`, compiles via the liftoff pipeline, builds a
         * fresh `Module` instance + `ImportSpec`, and attaches them to
         * @p entry via `Prepare`. Does **not** run the top-level — that is
         * the executor fiber's job, spawned by the caller (`Import`) after
         * this returns.
         *
         * Self-Fail contract: on any failure path (open, compile,
         * allocation), this calls `Fail(entry)` before returning an empty
         * handle. The caller therefore must NOT call `Fail` again on
         * receiving an empty result — just propagate ERROR.
         *
         * @param key    Canonical import key (used for diagnostics and as
         *               the module's short name via basename).
         * @param desc   Descriptor produced by the locator chain; in
         *               particular `desc.origin` is the on-disk path.
         * @param code   Out: the compiled `Code` (kept alive by the caller
         *               so it can be handed to the executor's `Eval`).
         * @param entry  The LOADING entry obtained from `Acquire`.
         *
         * @return The freshly-built module (already Prepare'd onto
         *         @p entry), or an empty handle on failure (already
         *         Fail'd).
         */
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
         * @brief Low-level "alloc a fresh LOADING entry and put it in the
         *        map" helper.
         *
         * Assumes the caller (`Acquire`) holds `cache_lock_` in unique mode
         * and has just confirmed a miss for @p key — there is no re-check
         * here. `ModuleEntryNew` is invoked (so `owner` stays nullptr; the
         * SOURCE path sets it later via `BlockOnExecutor`), an `HEntry` is
         * allocated and inserted in `modules_`, and the key gets its own
         * INCREF for the HEntry (matched by the DECREF in `~Importer` and
         * `Fail`).
         *
         * @param key  Canonical, absolute import key.
         *
         * @return The fresh entry, or nullptr on allocation failure (any
         *         partially-allocated state is rolled back).
         */
        ModuleEntry *Insert(ORString *key);

        /**
         * @brief Peek at a cache entry by canonical key.
         *
         * Takes the cache shared lock for the duration of the lookup;
         * returns the entry verbatim (including LOADING ones) or nullptr
         * on miss. Inherently racy in the cross-fiber case — the entry
         * may transition out of LOADING (or be removed) right after the
         * shared lock is released. Use `Acquire` for the orchestration
         * path; `Lookup` is meant for diagnostics / fast-path checks
         * where staleness is acceptable.
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
         * @param entry   The LOADING entry obtained from `Acquire/FRESH`.
         * @param module  The (typically empty) module to attach. INCREF'd.
         * @param spec    The public `ImportSpec`. INCREF'd.
         */
        void Prepare(ModuleEntry *entry, Module *module, ImportSpec *spec);

        /**
         * @brief Attach @p module and @p spec **and** mark @p entry LOADED
         *        in a single locked step.
         *
         * Used by the BUILTIN / VIRTUAL paths, where the module is already
         * ready-made (no top-level to run) and there is no observable
         * "Prepare'd but not yet Committed" window. Functionally a fused
         * `Prepare` + state transition under one lock acquisition.
         *
         * Same assertions as `Prepare`: entry must be LOADING with no
         * module/spec set yet.
         *
         * @param entry   The LOADING entry obtained from `Acquire/FRESH`.
         * @param module  The ready-made module to attach. INCREF'd.
         * @param spec    The public `ImportSpec`. INCREF'd.
         */
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

        /**
         * @brief Top-level import on this isolate's importer.
         *
         * Same contract as the free-function `Import` (which simply
         * delegates here); see its docstring below for the full pipeline,
         * the cross-fiber BLOCKED protocol, and the cycle handling.
         */
        [[nodiscard]] ImportStatus Import(ORString *raw, const ImportSpec *origin, Module * &out_module) noexcept;

        [[nodiscard]] Isolate *GetIsolate() const noexcept {
            return this->isolate_;
        }

        [[nodiscard]] List *Roots() const {
            return this->roots_.get();
        }

        /**
         * @brief Publish @p entry as LOADED and wake every waiter.
         *
         * Called by the fiber-completion hook (`Orbiter::PublishResult`)
         * when the executor's top-level returns cleanly. Under the cache
         * unique lock:
         *   - flips state LOADING → LOADED;
         *   - clears `entry->owner` (the executor is about to be reclaimed);
         *   - drains `entry->waiters`: for each waiter, removes its
         *     wait-for edge from `wait_for_` and re-schedules it via
         *     `Orbiter::PushFiber`.
         *
         * `module` / `spec` are NOT attached here — they were set earlier
         * by `Prepare` so same-fiber cyclic imports could observe the
         * partial module while the top-level was running. Commit only
         * flips the visibility flag.
         *
         * Assertions: @p entry must be LOADING with `module` and `spec`
         * both attached.
         *
         * @param entry  The LOADING entry being published.
         */
        void Commit(ModuleEntry *entry);

        /**
         * @brief Drop a failed entry, propagating the error to every
         *        waiter and allowing future imports to retry.
         *
         * Called from two places:
         *   1. The fiber-completion hook (`Orbiter::PublishResult`) when
         *      the executor's top-level raised an unhandled panic. The
         *      current fiber (the executor itself) holds the panic, which
         *      Fail reads via `Fiber::Current()->GetPanicError()` and
         *      pushes onto every waiter's panic chain (`fiber->Panic(...)`).
         *   2. The importer directly, from `LoadScriptSource` on a
         *      compile / open / allocation failure — in that case the
         *      entry has no waiters yet (the executor hasn't been spawned)
         *      and Fail just cleans the cache; the importer's own panic
         *      stays set by the failing helper.
         *
         * Under the cache unique lock:
         *   - flips state LOADING → FAILED (documentary; the entry is in
         *     transit to removal);
         *   - drains `entry->waiters`: for each waiter clears its wait-for
         *     edge, sets its panic to the executor's error, re-schedules
         *     it via `Orbiter::PushFiber`;
         *   - removes @p entry from `modules_` and frees both the HEntry
         *     (with the matching DECREF on its key) and the `ModuleEntry`
         *     itself.
         *
         * Since the entry is removed from the cache, a subsequent `Import`
         * on the same key starts fresh — the "retry allowed" semantics
         * from `import/README.md`.
         *
         * Null-safe (a no-op on `entry == nullptr`). The pointer is
         * **consumed** otherwise — do not reference it after Fail returns.
         *
         * @param entry  The LOADING entry to drop, or nullptr.
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

    /**
     * @brief Variant of `Import` that takes the importing module directly
     *        instead of its `ImportSpec`.
     *
     * Intended convenience overload for call sites that have a `Module *`
     * (e.g. the VM's `IMPORT` opcode handler reading the current frame's
     * module) but not its spec. Should derive the spec from @p base and
     * forward to the canonical overload.
     *
     * @note Not yet implemented — asserts. Wire it up when the opcode
     *       integration lands.
     */
    ImportStatus Import(const Isolate *isolate, ORString *raw, const Module *base, Module * &out_module);
}

#endif // !ORBIT_ORBITER_IMPORT_IMPORTER_H_
