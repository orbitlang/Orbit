// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_IMPORT_IMPORTER_H_
#define ORBIT_ORBITER_IMPORT_IMPORTER_H_

#include <orbit/orbiter/datatype/list.h>

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

    class Importer {
        Isolate *isolate_;

        /// Ordered list of root paths (ORString elements). Append = lowest
        /// precedence. Held as a strong handle for the Importer's lifetime.
        datatype::HList roots_;

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
        bool AddRoot(datatype::ORString *path) const;

        [[nodiscard]] Isolate *GetIsolate() const noexcept {
            return this->isolate_;
        }

        [[nodiscard]] datatype::List *Roots() const {
            return this->roots_.get();
        }
    };
}

#endif // !ORBIT_ORBITER_IMPORT_IMPORTER_H_
