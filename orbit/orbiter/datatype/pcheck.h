// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_PCHECK_H_
#define ORBIT_ORBITER_DATATYPE_PCHECK_H_

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
#define PCHECK_ENTRIES(name, ...)       static constexpr Parameter name[] = {__VA_ARGS__, {{}, nullptr, false}}
#define PCHECK_DEF(name, optional, ...)                                         \
    {[]() constexpr -> U32 {                                                    \
        U32 m = 0;                                                              \
        for (const auto t : std::initializer_list<InstanceType>{__VA_ARGS__})   \
            m |= (1u << (U32)t);                                                \
                                                                                \
        return m;                                                               \
    }(), name, true, optional}

#define PCHECK_CHECK(name)                                          \
    do {                                                            \
        if(!orbiter::datatype::CheckParameter(name, argv, argc))    \
            return {};                                              \
    } while(0)

    struct Parameter {
        U32 types;

        const char *name;

        bool instance;
        bool optional;
    };

    bool CheckParameter(const Parameter *parameters, OObject **argv, U16 argc);
}

#endif // !ORBIT_ORBITER_DATATYPE_PCHECK_H_
