// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/util/macros.h>

#ifdef _ORBIT_PLATFORM_WINDOWS

#include <orbit/orbiter/support/nt/windows.h>

#include <orbit/orbiter/support/nt/nt.h>

using namespace orbiter::support::nt;

int orbiter::support::nt::GetExecutablePath(char *out_buf, int size) {
    if ((size = GetModuleFileNameA(nullptr, out_buf, size)) == 0)
        size = -1;

    return size;
}

#endif
