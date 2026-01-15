// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_ERRORS_H_
#define ORBIT_ORBITER_DATATYPE_ERRORS_H_

#include <orbit/datatype.h>

namespace orbiter::datatype {
    struct FFIError {
        enum Reason : U8 {
            ID,

            INVALID_ARITY,
            INVALID_FP_ARITY,
            ARITY_MISMATCH,
            CONVERSION_FAILED,
            UNSUPPORTED_TYPE,
            UNSUPPORTED_RET_FP_TYPE
        };

        static constexpr const char *Details[] = {
            (const char *) "FFIError",

            (const char *) "too many arguments provided to native function. Maximum supported is %d.",
            (const char *) "too many decimal arguments provided to native function. Maximum supported is %d.",
            (const char *) "native function '%s' expects %d arguments, but %d were provided",
            (const char *) "cannot convert Orbit %s to required native type %s(%s :%s)",
            (const char *) "%s does not support native conversion to %s",
            (const char *) "Orbit does not support float/double return values on this architecture"
        };
    };

    struct NotImplementedError {
        enum Reason : U8 {
            ID,

            DEFAULT,
            METHOD,
            OPERATOR
        };

        static constexpr const char *Details[] = {
            (const char *) "NotImplementedError",

            (const char *) "not implemented",
            (const char *) "you must implement method %s",
            (const char *) "operator '%s' not supported between '%s' and '%s'"
        };
    };

    struct MemoryError {
        enum Reason : U8 {
            ID,

            ESTACK,
            HEAP,
            STACK
        };

        static constexpr const char *Details[] = {
            (const char *) "OOMError",

            (const char *) "insufficient memory to create exception handling context",
            (const char *) "insufficient heap memory to complete allocation",
            (const char *) "stack overflow - maximum stack size exceeded"
        };
    };

    struct RuntimeError {
        enum Reason : U8 {
            ID,

            NATIVE_LOAD_LIBRARY,
            NATIVE_LOAD_SYMBOL,
            NATIVE_UNLOAD_LIBRARY
        };

        static constexpr const char *Details[] = {
            (const char *) "RuntimeError",

            (const char *) "failed to load native library '%s': %s",
            (const char *) "failed to load native symbol '%s': %s",
            (const char *) "failed to unload native library '%s': %s"
        };
    };

    struct TypeError {
        enum Reason : U8 {
            ID,

            PARAMETER,
            PANIC
        };

        static constexpr const char *Details[] = {
            (const char *) "TypeError",

            (const char *) "unexpected type '%s' for '%s' parameter(%d)",
            (const char *) "panic expect type '%s'",
        };
    };

    struct ValueError {
        enum Reason : U8 {
            ID,

            PARAMETER,
        };

        static constexpr const char *Details[] = {
            (const char *) "ValueError",

            (const char *) "unexpected '%s' value for '%s' parameter(%d)"
        };
    };
}

#endif // !ORBIT_ORBITER_DATATYPE_ERRORS_H_
