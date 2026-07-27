// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/memory/gc.h>

#include <orbit/orbiter/module/modules.h>

using namespace orbiter::datatype;
using namespace orbiter::module;

// *********************************************************************************************************************
// PRIMITIVES
// *********************************************************************************************************************

RUNTIME_FUNCTION(gc_collect, collect,
                 R"DOC(
@brief Force a full garbage collection cycle now.

Runs the collector across every generation immediately, regardless of the
allocation thresholds that drive automatic collection, and reports how many
objects it reclaimed.

The cycle is stop-the-world: it waits for the other mutators to reach a
safepoint, collects, then resumes them. Objects still reachable from the VM
registers, stacks and live handles are retained; only unreachable ones are
freed.

@return The number of objects reclaimed by this cycle.

@example
    let freed = gc.collect()
    io.print("reclaimed", freed, "objects")
)DOC", 0, nullptr, false, false) {
    auto *isolate = O_GET_ISOLATE(_func);

    return HOObject(UIntNew(isolate, isolate->gc->ForceCollect()));
}

RUNTIME_FUNCTION(gc_rearm, rearm,
                 R"DOC(
@brief Re-arm an object's finalizer so its `cleanup` runs again.

A `cleanup` block runs once, when the object is first collected. If the cleanup
resurrects the object (by making it reachable again, e.g. storing `self` in a
still-live place) the finalizer is left disarmed and will NOT run a second time
when the object dies again. Calling `rearm(obj)` inside the cleanup re-registers
the object with the collector, so the next time it becomes unreachable its
`cleanup` fires once more.

@param obj The (resurrected) object whose finalizer should be re-armed.

@example
    class Pool {
        cleanup {
            if (should_keep(self)) {
                revive(self)         # resurrect
                gc.rearm(self)       # run cleanup again on the next death
            }
        }
    }
)DOC", 1, nullptr, false, false) {
    auto *isolate = O_GET_ISOLATE(_func);

    // Only heap objects carry a finalizer; a small integer or oddball has none.
    if (!O_IS_OBJECT(argv[0])) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            "cannot re-arm the finalizer of a non-object '%s'",
                            nullptr,
                            argv[0]);

        return {};
    }

    auto *head = GC_GET_HEAD(argv[0]);
    head->SetFinalize(false);

    return HOObject(kOddBallNIL);
}

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

const ModuleEntry gc_entries[] = {
    ORBIT_MODULE_EXPORT_FUNCTION(gc_collect),
    ORBIT_MODULE_EXPORT_FUNCTION(gc_rearm),

    ORBIT_MODULE_SENTINEL
};

ModuleInit ModuleGC = {
    "::orbit::gc",
    "@brief Garbage collector control."
    "\n\n"
    "Exposes manual control over the garbage collector: force a full "
    "collection cycle on demand, learn how many objects were reclaimed, and "
    "re-arm the finalizer of a resurrected object.",
    "1.0.0",
    gc_entries,
    nullptr,
    nullptr
};

const ModuleInit *orbiter::module::module_gc_ = &ModuleGC;
