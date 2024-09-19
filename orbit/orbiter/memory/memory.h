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
    const auto MemoryCompare = stratum::util::MemoryCompare;
    const auto MemoryCopy = stratum::util::MemoryCopy;
    const auto MemoryZero = stratum::util::MemoryZero;

    const auto MemoryInit = stratum::Initialize;
    const auto MemoryFinalize = stratum::Finalize;

    void *Alloc(MSize size);

    void *Calloc(MSize size);

    void *Realloc(void *ptr, MSize size);

    void Free(void *ptr);
}

#endif // !ORBIT_ORBITER_MEMORY_MEMORY_H_
