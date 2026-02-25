// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/nativefunc.h>

#ifdef _ORBIT_PLATFORM_WINDOWS

#define __FFI_INTERNAL

#include <orbit/orbiter/native/ffi.h>
#include <orbit/orbiter/native/ffi_internal.h>

using namespace orbiter::native;
using namespace orbiter::datatype;

bool orbiter::native::CallFunction(Isolate *isolate, HOObject &out, const NativeFunc *func, OObject **args,
                                   const U16 argc) {
    return true;
}

#endif
