// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_ISOLATE_H_
#define ORBIT_ORBITER_ISOLATE_H_

#include <orbit/orbiter/datatype/obase.h>

namespace orbiter {
    struct Isolate {
        datatype::TypeInfo *primitive[datatype::kInstanceTypeCount];
    };

    Isolate *IsolateInit();
}

#endif // !ORBIT_ORBITER_ISOLATE_H_
