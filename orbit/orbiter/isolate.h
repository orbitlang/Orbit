// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_ISOLATE_H_
#define ORBIT_ORBITER_ISOLATE_H_

#include <stratum/memory.h>

#include <orbit/orbiter/datatype/obase.h>

#include <orbit/orbiter/memory/gc.h>

namespace orbiter {
    class Isolate {
        stratum::Memory *allocator_;

        friend class memory::IsolateAllocator;

    public:
        class FiberPool *fpool_;

        datatype::TypeInfo *primitive[datatype::kInstanceTypeCount];

        memory::GC *gc;

        Isolate() = delete;

        ~Isolate();

        static Isolate *New();
    };
}

#endif // !ORBIT_ORBITER_ISOLATE_H_
