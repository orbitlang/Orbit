// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_UTIL_HASH_H_
#define ORBIT_UTIL_HASH_H_

#include <orbit/util/macros.h>

#include <orbit/datatype.h>

#ifdef _ORBIT_ENVIRON_32BIT
#define FNV_PRIME 16777619
#define FNV_OFFSET_BASIS 2166136261
#else
#define FNV_PRIME 1099511628211ULL
#define FNV_OFFSET_BASIS 14695981039346656037ULL
#endif

inline MSize fnv1_hash(const unsigned char *data, size_t length) {
    auto hash = FNV_OFFSET_BASIS;
    for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

#endif // !ORBIT_UTIL_HASH_H_
