// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/isolate.h>

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

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

const ModuleEntry gc_entries[] = {
    ORBIT_MODULE_EXPORT_FUNCTION(gc_collect),

    ORBIT_MODULE_SENTINEL
};

ModuleInit ModuleGC = {
    "::orbit::gc",
    "@brief Garbage collector control."
    "\n\n"
    "Exposes manual control over the garbage collector: force a full "
    "collection cycle on demand and learn how many objects were reclaimed.",
    "1.0.0",
    gc_entries,
    nullptr,
    nullptr
};

const ModuleInit *orbiter::module::module_gc_ = &ModuleGC;
