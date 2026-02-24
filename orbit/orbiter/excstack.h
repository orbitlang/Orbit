// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_VMEXCSTACK_H_
#define ORBIT_ORBITER_VMEXCSTACK_H_

#include <orbit/orbiter/isolate.h>

namespace orbiter {
    constexpr PtrSize kExceptionContextTag = (0xECECEC << 1) | 0x1;

    struct ExceptionContext {
        PtrSize _sentinel_;

        ExceptionContext *prev;

        struct {
            U32 ret_pops: 30;
            U32 action: 2;
        };

        U32 coffset;
        U32 foffset;

        PtrSize key;
        PtrSize ret_value;
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_VMEXCSTACK_H_
