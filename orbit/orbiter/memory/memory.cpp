// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/memory/memory.h>

using namespace orbiter::memory;

void *orbiter::memory::Alloc(MSize size) {
    auto *mem = stratum::Alloc(size);
    // TODO: Raise Orbit exception
    return mem;
}

void *orbiter::memory::Calloc(MSize size) {
    auto *mem = stratum::Calloc(size);
    // TODO: Raise Orbit exception
    return mem;
}

void orbiter::memory::Free(void *ptr) {
    stratum::Free(ptr);
}

void *orbiter::memory::Realloc(void *ptr, MSize size) {
    auto *mem = stratum::Realloc(ptr, size);
    // TODO: Raise Orbit exception
    return mem;
}
