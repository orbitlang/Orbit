// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_ERRORS_H_
#define ORBIT_ORBITER_DATATYPE_ERRORS_H_

#include <orbit/datatype.h>

namespace orbiter::datatype {
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
