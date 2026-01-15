// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_NATIVE_FFI_INTERNAL_H_
#define ORBIT_ORBITER_NATIVE_FFI_INTERNAL_H_

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/nativefunc.h>

#ifndef __FFI_INTERNAL
#error "This file is internal to the FFI engine and should not be included directly"
#endif

namespace orbiter::native {
    constexpr auto kMaxSupportedArity = 16;
    constexpr auto kMaxSupportedFpArity = 8;

    struct ParamInfo {
        void *value;
        void *fp_reg;
    };

    bool PrepareCall(Isolate *isolate, const datatype::NativeFunc *func, ParamInfo *dst, double *fp_dst,
                     U16 *out_fp_length, datatype::OObject **args, U16 argc);

    datatype::HOObject ConvertToOrbitObject(Isolate *isolate, void **result, datatype::NativeType type);
} // orbiter::native

#endif // !ORBIT_ORBITER_NATIVE_FFI_INTERNAL_H_
