// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/util/macros.h>

#if defined(_ORBIT_PLATFORM_DARWIN)

#include <mach-o/dyld.h>
#include <sys/syslimits.h>

#elif defined(_ORBIT_PLATFORM_LINUX)

#include <limits.h>
#include <unistd.h>

#elif defined(_ORBIT_PLATFORM_WINDOWS)

#include <orbit/orbiter/support/nt/nt.h>

#endif

#include <cstdint>

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/support/process.h>

using namespace orbiter;
using namespace orbiter::datatype;

HORString support::GetExecutableName(Isolate *isolate) noexcept {
    char path[PATH_MAX];

#if defined(_ORBIT_PLATFORM_WINDOWS)

    const auto len = nt::GetExecutablePath(path, (int) sizeof(path));
    if (len < 0)
        return ORStringIntern(isolate, (unsigned char *) "", 0);

    return ORStringNew(isolate, path, (MSize) len);

#elif defined(_ORBIT_PLATFORM_LINUX)

    // readlink writes up to `bufsiz` bytes but does NOT NUL-terminate.
    // Reserve one byte for the terminator we add manually below.
    const auto len = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len < 0)
        return ORStringIntern(isolate, (unsigned char *) "", 0);

    path[len] = '\0';

    return ORStringNew(isolate, path, (MSize) len);

#elif defined(_ORBIT_PLATFORM_DARWIN)

    // `_NSGetExecutablePath` uses `buf_size` as an in/out parameter:
    //   - In:  capacity of `path` in bytes.
    //   - Out (only on failure): size required to hold the result.
    // Returns 0 on success (path is NUL-terminated) and -1 if the buffer
    // was too small.  We use a dedicated local for the size parameter so
    // the function's return value doesn't shadow it.
    auto buf_size = (uint32_t) sizeof(path);
    if (_NSGetExecutablePath(path, &buf_size) != 0)
        return ORStringIntern(isolate, (unsigned char *) "", 0);

    return ORStringNew(isolate, path);

#else

    return ORStringIntern(isolate, (unsigned char *) "", 0);

#endif
}

HORString support::GetExecutablePath(Isolate *isolate) noexcept {
    const auto name = GetExecutableName(isolate);
    if (!name)
        return {};

    const auto *buf = ORSTRING_TO_CSTR(name.get());
    const auto len = ORSTRING_LENGTH(name.get());

    // Walk back from the end to the last path separator.
    // Accept both `/` and `\\` because Windows tolerates
    // forward-slash paths at the OS layer.
    auto sep = (MSSize) len - 1;
    while (sep >= 0 && buf[sep] != '/' && buf[sep] != '\\')
        sep--;

    // No separator means a single-component name like `"orbit"` —
    // there is no meaningful directory to extract.
    if (sep < 0)
        return ORStringIntern(isolate, (unsigned char *) "", 0);

    return ORStringNew(isolate, buf, (MSize) sep + 1);
}
