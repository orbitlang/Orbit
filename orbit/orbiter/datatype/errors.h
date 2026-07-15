// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_ERRORS_H_
#define ORBIT_ORBITER_DATATYPE_ERRORS_H_

#include <orbit/datatype.h>

namespace orbiter::datatype {
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

    struct ImportError {
        enum Reason : U8 {
            ID,

            MODULE_NOT_FOUND,
            INVALID_KEY,
            NO_ORIGIN,
            INVALID_ORIGIN,
            LOADER_NOT_IMPLEMENTED,
        };

        static constexpr const char *Details[] = {
            (const char *) "ImportError",

            (const char *) "module '%s' not found; tried: %s",
            (const char *) "invalid import key '%s': %s",
            (const char *) "relative import '%s' has no origin",
            (const char *) "relative import '%s' is only valid from a source module",
            (const char *) "%s loader is not yet implemented (key '%s')",
        };
    };

    struct IndexError {
        enum Reason : U8 {
            ID,

            OUT_OF_RANGE,
        };

        static constexpr const char *Details[] = {
            (const char *) "IndexError",

            (const char *) "%s index %lld out of range [0, %lld)",
        };
    };

    struct KeyError {
        enum Reason : U8 {
            ID,

            NOT_FOUND,
        };

        static constexpr const char *Details[] = {
            (const char *) "KeyError",

            (const char *) "key not found in %s",
        };
    };

    struct MemoryError {
        enum Reason : U8 {
            ID,

            ESTACK,
            HEAP,
            STACK,
            NATIVE_ALLOC
        };

        static constexpr const char *Details[] = {
            (const char *) "OOMError",

            (const char *) "insufficient memory to create exception handling context",
            (const char *) "insufficient heap memory to complete allocation",
            (const char *) "stack overflow - maximum stack size exceeded",
            (const char *) "native memory allocation failed"
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

    struct OSError {
        enum Reason : U8 {
            ID,

            NOT_FOUND,          // ENOENT              / ERROR_FILE_NOT_FOUND
            PERMISSION_DENIED,  // EACCES, EPERM       / ERROR_ACCESS_DENIED
            ALREADY_EXISTS,     // EEXIST              / ERROR_FILE_EXISTS
            BROKEN_PIPE,        // EPIPE               / ERROR_BROKEN_PIPE
            INTERRUPTED,        // EINTR
            BAD_FD,             // EBADF               / ERROR_INVALID_HANDLE
            WOULD_BLOCK,        // EAGAIN, EWOULDBLOCK / WSAEWOULDBLOCK
            TIMEOUT,            // ETIMEDOUT           / WSAETIMEDOUT
            INVALID_ARGUMENT,   // EINVAL              / ERROR_INVALID_PARAMETER
            NO_MEMORY,          // ENOMEM              / ERROR_NOT_ENOUGH_MEMORY

            // Catch-all for any errno value not mapped above.
            OTHER,
        };

        static constexpr const char *Details[] = {
            (const char *) "OSError",

            (const char *) "file or directory not found: %s",
            (const char *) "permission denied: %s",
            (const char *) "already exists: %s",
            (const char *) "broken pipe: %s",
            (const char *) "operation interrupted: %s",
            (const char *) "bad file descriptor: %s",
            (const char *) "operation would block: %s",
            (const char *) "operation timed out: %s",
            (const char *) "invalid argument: %s",
            (const char *) "out of memory: %s",
            (const char *) "OS error %d (%s): %s"
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
            NEGATIVE_SHIFT_COUNT,
            CONCURRENT_MODIFICATION,
            SUSPEND_IN_SYNC_CALL,
        };

        static constexpr const char *Details[] = {
            (const char *) "RuntimeError",

            (const char *) "failed to load native library '%s': %s",
            (const char *) "failed to load native symbol '%s': %s",
            (const char *) "failed to unload native library '%s': %s",
            (const char *) "'%s' object passed iterator check but does not implement iter_next",
            (const char *) "division by zero",
            (const char *) "negative shift count",
            (const char *) "%s changed size during iteration",
            (const char *) "cannot %s within a synchronous type method call",
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
            NON_SUBSCRIPTABLE,
            UNHASHABLE,
            GENERATOR_INVALID_CALL,
            INVALID_NATIVE_TYPE,
            GENERATOR_SPAWN,
            NON_SYNCHRONIZABLE,
            METHOD_RECEIVER,
            TOO_MANY_ARGS,
            NO_NAMED_ARGS,
            NO_KWARGS,
            INIT_NO_CURRY,
        };

        static constexpr const char *Details[] = {
            (const char *) "TypeError",

            (const char *) "expected type '%s', got '%s'",
            (const char *) "unexpected type '%s' for '%s' parameter(%d)",
            (const char *) "panic expect type '%s'",
            (const char *) "invalid call to a non-callable object('%s')",
            (const char *) "'%s' object is not iterable",
            (const char *) "'%s' object is not subscriptable",
            (const char *) "unhashable type: '%s'",
            (const char *) "cannot pass arguments when resuming a generator",
            (const char *) "invalid call: native functions can only be called within their defining module by directly "
            "invoking the symbol (indirect calls are not allowed)",
            (const char *) "'spawn' does not support generator functions",
            (const char *) "'%s' object cannot be used as a sync target",
            (const char *) "method call requires '%s' instance as receiver",
            (const char *) "'%s' takes %d positional argument(s) but %d were given",
            (const char *) "'%s' does not accept named arguments",
            (const char *) "'%s' does not accept keyword arguments",
            (const char *) "constructor of '%s' cannot be partially applied",
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
