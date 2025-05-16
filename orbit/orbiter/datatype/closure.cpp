// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/closure.h>
#include <orbit/orbiter/datatype/function.h>

using namespace orbiter::datatype;

// TODO: Mutex lock

bool ClosureDtor(const Closure *self) {
    const auto slots = (OObject **) ((unsigned char *) self + sizeof(Closure));

    for (int i = 0; i < self->slots; i++)
        O_DECREF(slots[i]);

    return true;
}

bool orbiter::datatype::ClosureTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) ClosureDtor;

    return true;
}

HClosure orbiter::datatype::ClosureNew(Isolate *isolate, U16 slots) {
    auto closure = MakeObject<Closure>(isolate, InstanceType::CLOSURE, slots * sizeof(void *));

    if (closure != nullptr) {
        closure->slots = slots;

        stratum::util::MemoryZero(((unsigned char *) closure) + sizeof(Closure), slots * sizeof(void *));
    }

    // This is treated as a non-container object for garbage collection purposes
    O_GC_TRACK_RETURN(isolate, closure, false);
}

HOObject orbiter::datatype::ClosureGet(Closure *closure, U16 index) {
    const auto slots = (OObject **) ((unsigned char *) closure + sizeof(Closure));

    return HOObject(slots[index]);
}

HOType orbiter::datatype::ClosureTypeInit(Isolate *isolate) {
    auto type = MakeType(isolate, InstanceType::CLOSURE, sizeof(Closure) - sizeof(OObject), 0, 0);
    return type;
}

void orbiter::datatype::ClosureSet(Closure *closure, U16 index, OObject *object) {
    const auto slots = (OObject **) ((unsigned char *) closure + sizeof(Closure));

    O_DECREF(slots[index]);

    slots[index] = O_INCREF(object);
}

