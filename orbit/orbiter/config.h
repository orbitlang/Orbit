// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_CONFIG_H_
#define ORBIT_ORBITER_CONFIG_H_

#include <orbit/datatype.h>

namespace orbiter {
    struct Config {
        I32 ost_max;
        I32 vc_max;
        I32 fiber_ssize;
        I32 fiber_pool;
    };

    extern const Config *kConfigDefault;
} // namespace orbiter

#endif // !ORBIT_ORBITER_CONFIG_H_
