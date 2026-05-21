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

    /// Host filesystem path separator — used when building paths that go to
    /// the OS (stat, open, …). `\` on Windows, `/` everywhere else.
#if defined(_ORBIT_PLATFORM_WINDOWS)
    constexpr auto *kHostPathSep = "\\";
#else
    constexpr auto *kHostPathSep = "/";
#endif

    /// Module registry hashmap: canonical key → ModuleEntry. Plain
    /// engine-side container (entries are not Orbit objects).
    using ModuleMap = HashMap<ORString *,
        ModuleEntry *,
        ORStringEqual,
        ORStringHash>;

    class Importer {
        Isolate *isolate_;

        /// Ordered list of root paths (ORString elements). Append = lowest
        /// precedence. Held as a strong handle for the Importer's lifetime.
        HList roots_;

        /// Cache of canonical key → ModuleEntry. Stores both loaded and
        /// in-progress (LOADING) modules. See `import/registry.h`.
        ModuleMap modules_;

        /// Brief lock around `modules_`. Held only for cache operations —
        /// **never** during a module's top-level execution. The "no lock
        /// during load" rule from `import/README.md` is what keeps nested
        /// imports from deadlocking the registry on themselves.
        sync::AsyncRWLock cache_lock_;

    public:
        explicit Importer(Isolate *isolate) : isolate_(isolate), modules_(isolate) {
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
        bool AddRoot(const char *path);

        /// Append an already-built root string (lowest precedence).
        bool AddRoot(ORString *path) const;

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
         * @param was_inserted   Optional out: set to true when a fresh
         *                       LOADING entry was created, false when the
         *                       returned entry was already in the cache.
         *                       Pass `nullptr` to ignore.
         *
         * @return The entry (fresh or pre-existing), or nullptr on
         *         allocation failure (isolate panic set).
         */
        ModuleEntry *Insert(ORString *key, bool *was_inserted);

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
        void Prepare(ModuleEntry *entry, OObject *module, ImportSpec *spec);

        void PrepareCommit(ModuleEntry *entry, OObject *module, ImportSpec *spec);

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
         * The pointer is **consumed**: do not reference it after Fail
         * returns.
         *
         * @param entry  The LOADING entry obtained from a prior `Insert`.
         */
        void Fail(ModuleEntry *entry);

        [[nodiscard]] Isolate *GetIsolate() const noexcept {
            return this->isolate_;
        }

        [[nodiscard]] List *Roots() const {
            return this->roots_.get();
        }
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
     * @brief Top-level import: produce the module for @p raw, loading it if
     *        necessary.
     *
     * The full pipeline (see `import/README.md`):
     *   1. Canonicalize @p raw against @p origin → key.
     *   2. Lookup in the registry: LOADED returns the module immediately;
     *      LOADING returns the partial module (same-fiber cycles).
     *   3. On miss, Insert a fresh LOADING entry (re-checking under the
     *      cache unique lock for race safety; another fiber may have
     *      inserted between our Lookup and the Insert).
     *   4. Resolve the key via the locator chain → Descriptor.
     *   5. Dispatch on `Descriptor::kind` to the matching loader: BUILTIN
     *      and VIRTUAL adopt the ready-made module as-is; SOURCE/NATIVE
     *      are not yet implemented (will fail with `LOADER_NOT_IMPLEMENTED`).
     *   6. `Prepare` + `Commit` the entry; on any failure `Fail` it so a
     *      future import may retry.
     *
     * @param isolate  Owning isolate.
     * @param raw      The raw import string from source.
     * @param origin   ImportSpec of the importing module, or nullptr for
     *                 top-level imports.
     *
     * @return Handle to the loaded module, or empty on failure (isolate
     *         panic set with the originating error).
     */
    HOObject Import(Isolate *isolate, ORString *raw, const ImportSpec *origin);
}

#endif // !ORBIT_ORBITER_IMPORT_IMPORTER_H_
