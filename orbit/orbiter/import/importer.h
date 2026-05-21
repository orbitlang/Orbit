// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_IMPORT_IMPORTER_H_
#define ORBIT_ORBITER_IMPORT_IMPORTER_H_

#include <orbit/orbiter/datatype/list.h>

#include <orbit/orbiter/import/locator.h>

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

    class Importer {
        Isolate *isolate_;

        /// Ordered list of root paths (ORString elements). Append = lowest
        /// precedence. Held as a strong handle for the Importer's lifetime.
        HList roots_;

    public:
        explicit Importer(Isolate *isolate) : isolate_(isolate) {
        }

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

        [[nodiscard]] Isolate *GetIsolate() const noexcept {
            return this->isolate_;
        }

        [[nodiscard]] datatype::List *Roots() const {
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
}

#endif // !ORBIT_ORBITER_IMPORT_IMPORTER_H_
