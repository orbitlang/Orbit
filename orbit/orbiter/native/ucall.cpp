// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/util/macros.h>

#ifndef _ORBIT_PLATFORM_WINDOWS

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/nativefunc.h>

#define __FFI_INTERNAL

#include <orbit/orbiter/native/ffi.h>
#include <orbit/orbiter/native/ffi_internal.h>

using namespace orbiter::native;
using namespace orbiter::datatype;

using f0 = void *(*)();
using f1 = void *(*)(void *);
using f2 = void *(*)(void *, void *);
using f3 = void *(*)(void *, void *, void *);
using f4 = void *(*)(void *, void *, void *, void *);
using f5 = void *(*)(void *, void *, void *, void *, void *);
using f6 = void *(*)(void *, void *, void *, void *, void *, void *);
using f7 = void *(*)(void *, void *, void *, void *, void *, void *, void *);
using f8 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *);
using f9 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *, void *);
using f10 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using f11 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using f12 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
using f13 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *,
                      void *);
using f14 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *,
                      void *, void *);
using f15 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *,
                      void *, void *, void *);
using f16 = void *(*)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *,
                      void *, void *, void *, void *);

#ifdef ORBIT_HAS_FFI_STUB
extern "C" {
double fpu_get_return();

void fpu_preload(double *floats);
}
#endif

void *FFICall(void *func, const orbiter::native::ParamInfo *args, const U16 arity) {
    switch (arity) {
        case 0:
            return ((f0) func)();
        case 1:
            return ((f1) func)(args[0].value);
        case 2:
            return ((f2) func)(args[0].value, args[1].value);
        case 3:
            return ((f3) func)(args[0].value, args[1].value, args[2].value);
        case 4:
            return ((f4) func)(args[0].value, args[1].value, args[2].value, args[3].value);
        case 5:
            return ((f5) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value);
        case 6:
            return ((f6) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value,
                               args[5].value);
        case 7:
            return ((f7) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value, args[5].value,
                               args[6].value);
        case 8:
            return ((f8) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value, args[5].value,
                               args[6].value,
                               args[7].value);
        case 9:
            return ((f9) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value, args[5].value,
                               args[6].value,
                               args[7].value, args[8].value);
        case 10:
            return ((f10) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value,
                                args[5].value, args[6].value,
                                args[7].value, args[8].value, args[9].value);
        case 11:
            return ((f11) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value,
                                args[5].value, args[6].value,
                                args[7].value, args[8].value, args[9].value, args[10].value);
        case 12:
            return ((f12) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value,
                                args[5].value, args[6].value,
                                args[7].value, args[8].value, args[9].value, args[10].value, args[11].value);
        case 13:
            return ((f13) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value,
                                args[5].value, args[6].value,
                                args[7].value, args[8].value, args[9].value, args[10].value, args[11].value,
                                args[12].value);
        case 14:
            return ((f14) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value,
                                args[5].value, args[6].value,
                                args[7].value, args[8].value, args[9].value, args[10].value, args[11].value,
                                args[12].value,
                                args[13].value);
        case 15:
            return ((f15) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value,
                                args[5].value, args[6].value,
                                args[7].value, args[8].value, args[9].value, args[10].value, args[11].value,
                                args[12].value,
                                args[13].value, args[14].value);
        case 16:
            return ((f16) func)(args[0].value, args[1].value, args[2].value, args[3].value, args[4].value,
                                args[5].value, args[6].value,
                                args[7].value, args[8].value, args[9].value, args[10].value, args[11].value,
                                args[12].value,
                                args[13].value, args[14].value, args[15].value);
        default:
            assert(false);
    }
}

bool orbiter::native::CallFunction(Isolate *isolate, HOObject &out, const NativeFunc *func, OObject **args,
                                   const U16 argc) {
    ParamInfo f_args[argc];

    double fp_args[kMaxSupportedFpArity];
    U16 fp_count = kMaxSupportedFpArity;

    const auto arity = func->arity;

    if (arity > kMaxSupportedArity) {
        ErrorSet(isolate,
                 FFIError::Details[FFIError::Reason::ID],
                 nullptr,
                 FFIError::Details[FFIError::Reason::INVALID_ARITY],
                 kMaxSupportedArity
        );

        return false;
    }

    if (!PrepareCall(isolate, func, f_args, fp_args, &fp_count, args, argc))
        return false;

#ifdef ORBIT_HAS_FFI_STUB
    if (fp_count > 0)
        fpu_preload(fp_args);
#else
    if (fp_count > 0 || func->ret_type == NativeType::F32 || func->ret_type == NativeType::F64) {
        ErrorSet(isolate,
                 FFIError::Details[FFIError::Reason::ID],
                 nullptr,
                 FFIError::Details[FFIError::Reason::UNSUPPORTED_RET_FP_TYPE]
        );

        return false;
    }
#endif

    void *result = FFICall(func->handle, f_args, arity);

#ifdef ORBIT_HAS_FFI_STUB
    if (func->ret_type == NativeType::F32)
        *((float *) (&result)) = (float) fpu_get_return();
    else if (func->ret_type == NativeType::F64)
        *((double *) (&result)) = fpu_get_return();
#endif

    out = std::move(ConvertToOrbitObject(isolate, &result, func->ret_type));

    return true;
}

#endif
