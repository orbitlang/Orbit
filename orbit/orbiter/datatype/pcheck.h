// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_PCHECK_H_
#define ORBIT_ORBITER_DATATYPE_PCHECK_H_

#include <initializer_list>

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
#define PCHECK_ENTRIES(name, ...)       static constexpr Parameter name[] = {__VA_ARGS__, {{}, nullptr, false}}
#define PCHECK_DEF(name, optional, ...) {{__VA_ARGS__}, name, true, optional}

#define PCHECK_CHECK(name)                                          \
    do {                                                            \
        if(!orbiter::datatype::CheckParameter(name, argv, argc))    \
            return {};                                              \
    } while(0)

    struct Parameter {
        const std::initializer_list<InstanceType> types;

        const char *name;

        bool instance;
        bool optional;
    };

    bool CheckParameter(const Parameter *parameters, OObject **argv, U16 argc);
}

#endif // !ORBIT_ORBITER_DATATYPE_PCHECK_H_
