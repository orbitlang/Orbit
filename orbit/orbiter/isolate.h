// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_ISOLATE_H_
#define ORBIT_ORBITER_ISOLATE_H_

#include <stratum/memory.h>

#include <orbit/orbiter/datatype/obase.h>

#include <orbit/orbiter/memory/gc.h>

#include <orbit/orbiter/panic.h>

#define O_GC_TRACK_RETURN(isolate, object, is_container)                \
    do{                                                                 \
            auto handle_ = Handle(object);                              \
            if(object != nullptr) {                                     \
                (isolate)->gc->Track((OObject *) object, is_container); \
            }                                                           \
            return handle_; } while (0)

namespace orbiter {
    namespace native {
        class Loader;
    }

    class Isolate {
        stratum::Memory *allocator_;

        friend class memory::IsolateAllocator;

    public:
        PanicContainer panic;

        Panic *panic_cache;

        datatype::OObject *oom_error_;

        class DeferPool *dpool_;
        class FiberPool *fpool_;
        native::Loader *loader_;

        datatype::TypeInfo *primitive[datatype::kInstanceTypeCount];

        memory::GC *gc;

        Isolate() = delete;

        ~Isolate();

        static Isolate *New();
    };
}

#endif // !ORBIT_ORBITER_ISOLATE_H_
