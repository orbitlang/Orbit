// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_NATIVE_FFI_H_
#define ORBIT_ORBITER_NATIVE_FFI_H_

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter {
    namespace datatype {
        struct NativeFunc;
    }

    namespace native {
        enum class NativeBindingType : U8 {
            FUNC,
            VAR,
            CONST
        };

        struct NativeParam {
            datatype::ORString *name;

            datatype::NativeType type;
        };

        struct NativeBinding {
            datatype::ORString *name;
            datatype::ORString *symbol;
            datatype::ORString *library;

            struct {
                NativeParam *params;
                U16 count;
            } params;

            datatype::NativeType ret_type;

            NativeBindingType binding_type;
        };

        bool CallFunction(Isolate *isolate, datatype::HOObject &out, const datatype::NativeFunc *func,
                          datatype::OObject **args, U16 argc);
    }
} // orbiter::native

#endif // !ORBIT_ORBITER_NATIVE_FFI_H_
