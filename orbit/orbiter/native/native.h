// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_NATIVE_NATIVE_H_
#define ORBIT_ORBITER_NATIVE_NATIVE_H_

namespace orbiter::native {
    enum class NativeType {
        BOOL,
        BYTE,

        I8,
        I16,
        I32,
        I64,
        ISIZE,

        U8,
        U16,
        U32,
        U64,
        USIZE,
        PTR,

        F32,
        F64,
    };
}

#endif // !ORBIT_ORBITER_NATIVE_NATIVE_H_
