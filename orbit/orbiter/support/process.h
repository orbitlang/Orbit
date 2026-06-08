// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_SUPPORT_PROCESS_H_
#define ORBIT_ORBITER_SUPPORT_PROCESS_H_

#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::support {
    /**
     * @brief Resolve the absolute path of the currently-running executable.
     *
     * Returns the full path to the binary as reported by the OS — a value
     * that an `argv[0]`-style lookup cannot reliably produce.  Each
     * platform uses a different primitive:
     *   - Darwin:  `_NSGetExecutablePath`
     *   - Linux:   `readlink("/proc/self/exe", ...)`
     *   - Windows: `GetModuleFileNameA` (via `support::nt`)
     *
     * On platforms not covered by the above, returns an empty interned
     * string — callers should still handle the "empty result" case.
     *
     * @param isolate  Owning isolate;
     *
     * @return Handle to the absolute exe path, or an empty interned
     *         string if the platform call failed.  An empty *handle*
     *         indicates an allocation failure (isolate panic set).
     */
    datatype::HORString GetExecutableName(Isolate *isolate) noexcept;

    /**
     * @brief Resolve the absolute path of the directory containing the
     *        running executable.
     *
     * Builds on `GetExecutableName` and trims off the trailing component
     * (the binary name).  When `GetExecutableName` produced something
     * with no path separator (e.g. just `"orbit"`), an empty interned
     * string is returned.  The result includes the trailing separator,
     * so concatenations against a leaf name immediately produce a
     * well-formed path.
     *
     * @param isolate  Owning isolate.
     *
     * @return Handle to the directory path, or an empty handle when the
     *         underlying executable lookup itself failed to allocate.
     */
    datatype::HORString GetExecutablePath(Isolate *isolate) noexcept;
}

#endif // !ORBIT_ORBITER_SUPPORT_PROCESS_H_
