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
            OPERATOR,
            UNARY_OPERATOR,
        };

        static constexpr const char *Details[] = {
            (const char *) "NotImplementedError",

            (const char *) "not implemented",
            (const char *) "you must implement method %s",
            (const char *) "operator '%s' not supported between '%s' and '%s'",
            (const char *) "operator '%s' not supported by '%s'"
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
            NATIVE_UNLOAD_LIBRARY,
            ITER_NEXT_NOT_IMPLEMENTED,
            ZERO_DIVISION,
            NEGATIVE_SHIFT_COUNT
        };

        static constexpr const char *Details[] = {
            (const char *) "RuntimeError",

            (const char *) "failed to load native library '%s': %s",
            (const char *) "failed to load native symbol '%s': %s",
            (const char *) "failed to unload native library '%s': %s",
            (const char *) "'%s' object passed iterator check but does not implement iter_next",
            (const char *) "division by zero",
            (const char *) "negative shift count"
        };
    };

    struct SchedulerError {
        enum Reason : U8 {
            ID,

            FIBER_QUEUE_FULL,
        };

        static constexpr const char *Details[] = {
            (const char *) "SchedulerError",

            (const char *) "failed to enqueue fiber for execution",
        };
    };

    struct StopIterationError {
        enum Reason : U8 {
            ID,

            GENERATOR_EXHAUSTED
        };

        static constexpr const char *Details[] = {
            (const char *) "StopIterationError",

            (const char *) "generator is exhausted",
        };
    };

    struct TypeError {
        enum Reason : U8 {
            ID,

            MISMATCH,
            PARAMETER,
            PANIC,
            NON_CALLABLE,
            NON_ITERABLE,
            GENERATOR_INVALID_CALL,
            INVALID_NATIVE_TYPE,
            GENERATOR_SPAWN,
            NON_SYNCHRONIZABLE,
            METHOD_RECEIVER,
        };

        static constexpr const char *Details[] = {
            (const char *) "TypeError",

            (const char *) "expected type '%s', got '%s'",
            (const char *) "unexpected type '%s' for '%s' parameter(%d)",
            (const char *) "panic expect type '%s'",
            (const char *) "invalid call to a non-callable object('%s')",
            (const char *) "'%s' object is not iterable",
            (const char *) "cannot pass arguments when resuming a generator",
            (const char *) "invalid call: native functions can only be called within their defining module by directly "
            "invoking the symbol (indirect calls are not allowed)",
            (const char *) "'spawn' does not support generator functions",
            (const char *) "'%s' object cannot be used as a sync target",
            (const char *) "method call requires '%s' instance as receiver"
        };
    };

    struct NameError {
        enum Reason : U8 {
            ID,

            NOT_DEFINED,
        };

        static constexpr const char *Details[] = {
            (const char *) "NameError",

            (const char *) "name '%s' is not defined"
        };
    };

    struct AttributeError {
        enum Reason : U8 {
            ID,

            NOT_FOUND,
            PRIVATE_ACCESS,
            CONSTANT_ASSIGN,
        };

        static constexpr const char *Details[] = {
            (const char *) "AttributeError",

            (const char *) "'%s' object has no property '%s'",
            (const char *) "cannot access private property '%s' of '%s'",
            (const char *) "cannot assign to constant property '%s'"
        };
    };

    struct ValueError {
        enum Reason : U8 {
            ID,

            PARAMETER,
            MISSING_PARAMETER,
        };

        static constexpr const char *Details[] = {
            (const char *) "ValueError",

            (const char *) "unexpected '%s' value for '%s' parameter(%d)",
            (const char *) "missing required parameter '%s' at position %d"
        };
    };

    struct UnicodeError {
        enum Reason : U8 {
            ID,

            INVALID_START_BYTE,
            INVALID_CONTINUATION_BYTE,
        };

        static constexpr const char *Details[] = {
            (const char *) "UnicodeError",

            (const char *) "can't decode byte 0x%02x: invalid start byte",
            (const char *) "can't decode byte 0x%02x: invalid continuation byte",
        };
    };
}

#endif // !ORBIT_ORBITER_DATATYPE_ERRORS_H_
