// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_ERRORS_H_
#define ORBIT_ORBITER_DATATYPE_ERRORS_H_

namespace orbiter::datatype {
    struct MemoryError {
        enum Reason : U8 {
            ID,

            HEAP,
            STACK
        };

        static constexpr const char *Details[] = {
            (const char *) "OOMError",

            (const char *) "Insufficient heap memory to complete allocation",
            (const char *) "Stack overflow - maximum stack size exceeded"
        };
    };

    struct TypeError {
        enum Reason : U8 {
            ID,

            PARAMETER
        };

        static constexpr const char *Details[] = {
            (const char *) "TypeError",

            (const char *) "unexpected type '%s' for '%s' parameter(%d)"
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
