// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/datatype/tuple.h>

using namespace orbiter::datatype;

bool TupleDtor(const Tuple *self) {
    for (auto i = 0; i < self->length; i++)
        O_DECREF(self->objects[i]);

    return true;
}

bool orbiter::datatype::TupleAppend(Tuple *tuple, OObject *item) {
    if (tuple->length == tuple->capacity)
        return false;

    if (tuple->objects[tuple->length] != nullptr)
        O_DECREF(tuple->objects[tuple->length]);

    tuple->objects[tuple->length] = O_INCREF(item);

    tuple->length += 1;

    return true;
}

bool orbiter::datatype::TupleTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) TupleDtor;

    return true;
}

Handle<Tuple> orbiter::datatype::TupleNew(Isolate *isolate, MSize count) {
    auto *tuple = MakeObject<Tuple>(isolate, InstanceType::TUPLE);

    if (tuple != nullptr) {
        memory::IsolateAllocator allocator(isolate);

        tuple->objects = allocator.alloc<OObject *>(count * sizeof(void *));
        if (tuple->objects == nullptr) {
            isolate->gc->RawFree((OObject *) tuple, false);

            return {};
        }

        tuple->length = count;
        tuple->hash = 0;
    }

    return Handle(tuple);
}

HOType orbiter::datatype::TupleTypeInit(Isolate *isolate) {
    auto tuple = MakeType(isolate, InstanceType::TUPLE, sizeof(Tuple) - sizeof(OObject), 0, 0);
    return tuple;
}
