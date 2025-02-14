// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MEMORY_MEMORY_H_
#define ORBIT_ORBITER_MEMORY_MEMORY_H_

#include <cstddef>

#include <orbit/datatype.h>

#include <stratum/memory.h>
#include <stratum/memutil.h>

#define ORBIT_ORBITER_MEMORY_QUANTUM (STRATUM_QUANTUM)

namespace orbiter::memory {
    constexpr size_t kToBytes = 1;
    constexpr size_t kToKBytes = 1024 * kToBytes; // 1 KB = 1024 byte
    constexpr size_t kToMBytes = 1024 * kToKBytes; // 1 MB = 1024 KB

    const auto MemoryCompare = stratum::util::MemoryCompare;
    const auto MemoryCopy = stratum::util::MemoryCopy;
    const auto MemoryZero = stratum::util::MemoryZero;
}

#endif // !ORBIT_ORBITER_MEMORY_MEMORY_H_
