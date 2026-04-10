// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <shared_mutex>

#include <orbit/orbiter/datatype/closure.h>
#include <orbit/orbiter/datatype/function.h>

using namespace orbiter::datatype;

void ClosureTrace(const Closure *self, const GCTraceCallback callback, const MSize epoch) {
    const auto slots = (OObject **) ((unsigned char *) self + sizeof(Closure));

    for (int i = 0; i < self->slots; i++)
        callback(slots[i], epoch);
}

bool orbiter::datatype::ClosureTypeSetup(TypeInfo *self) {
    self->trace = (TraceFn) ClosureTrace;

    return true;
}

HClosure orbiter::datatype::ClosureNew(Isolate *isolate, const U16 slots) {
    const auto closure = MakeObject<Closure>(isolate, InstanceType::CLOSURE, slots * sizeof(void *));
    if (closure != nullptr) {
        new(&closure->lock)sync::AsyncRWLock();

        closure->slots = slots;

        stratum::util::MemoryZero(((unsigned char *) closure) + sizeof(Closure), slots * sizeof(void *));
    }

    // This is treated as a non-container object for garbage collection purposes
    O_GC_TRACK_RETURN(isolate, closure, false);
}

HOObject orbiter::datatype::ClosureGet(Closure *closure, const U16 index) {
    const auto slots = (OObject **) ((unsigned char *) closure + sizeof(Closure));

    std::shared_lock _(closure->lock);

    return HOObject(slots[index]);
}

HOType orbiter::datatype::ClosureTypeInit(Isolate *isolate) {
    auto closure = MakeType(isolate, "Closure", InstanceType::CLOSURE, sizeof(Closure) - sizeof(OObject), 0, 0);
    return closure;
}

void orbiter::datatype::ClosureSet(Closure *closure, const U16 index, OObject *object) {
    const auto slots = (OObject **) ((unsigned char *) closure + sizeof(Closure));

    std::unique_lock _(closure->lock);

    slots[index] = object;
}
