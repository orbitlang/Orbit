// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_DATATYPE_H_
#define ORBIT_DATATYPE_H_

#include <cstdint>

typedef unsigned char Byte;
typedef unsigned char *Bytes;

typedef Byte U8;

typedef short I16;
typedef unsigned short U16;

typedef int I32;
typedef unsigned int U32;

typedef long I64;
typedef unsigned long U64;

typedef U64 PtrSize;
typedef U64 MSize;

#if SIZE_MAX == 0xFFFFFFFF
typedef U16 PtrHalfSize;
#elif SIZE_MAX == 0xFFFFFFFFFFFFFFFF
typedef U32 PtrHalfSize;
#else
#error "Unsupported pointer size"
#endif

#endif // !ORBIT_DATATYPE_H_
