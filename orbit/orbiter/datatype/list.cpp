// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/list.h>

using namespace orbiter::datatype;

bool ListCheckSize(List *list, MSize count) {
    MSize len = list->capacity + count;
    OObject **tmp;

    if (count == 0)
        len = (list->capacity + 1) + ((list->capacity + 1) / 2);

    if (list->length + count > list->capacity) {
        if (list->objects == nullptr)
            len = kListInitialCapacity;

        orbiter::memory::IsolateAllocator allocator(O_GET_TYPE(list)->isolate);

        if ((tmp = allocator.realloc(list->objects, len * sizeof(void *))) == nullptr)
            return false;

        list->objects = tmp;
        list->capacity = len;
    }

    return true;
}

void ListTrace(const List *self, GCTraceCallback callback, MSize epoch) {
    // TODO: Sync?!
    for (auto i = 0; i < self->length; i++) {
        const auto obj = self->objects[i];

        if (O_IS_OBJECT(obj))
            callback(obj, epoch);
    }
}

bool orbiter::datatype::ListTypeSetup(TypeInfo *self) {
    self->trace = (TraceFn) ListTrace;

    return true;
}

bool orbiter::datatype::ListAppend(List *list, OObject *object) {
    if (!ListCheckSize(list, 1))
        return false;

    list->objects[list->length++] = object;

    return true;
}

bool orbiter::datatype::ListAppend(List *list, const List *other) {
    if (other == nullptr)
        return true;

    if (!ListCheckSize(list, other->length))
        return false;

    for (MSize i = 0; i < other->length; i++)
        list->objects[list->length++] = other->objects[i];

    return true;
}

bool orbiter::datatype::ListInsert(List *list, OObject *object, MSize index) {
    if (index < 0)
        index = ((MSize) list->length) + index;

    if (index > list->length) {
        if (!ListCheckSize(list, 1))
            return false;

        list->objects[list->length++] = object;

        return true;
    }

    list->objects[index] = object;

    return true;
}

bool orbiter::datatype::ListPrepend(List *list, OObject *object) {
    if (!ListCheckSize(list, 1))
        return false;

    for (MSize i = list->length; i > 0; i--)
        list->objects[i] = list->objects[i - 1];

    list->objects[0] = object;

    list->length++;

    return true;
}

HList orbiter::datatype::ListNew(Isolate *isolate, MSize capacity) {
    auto *list = MakeObject<List>(isolate, InstanceType::LIST);
    if (list == nullptr)
        return {};

    list->objects = nullptr;
    list->capacity = capacity;
    list->length = 0;

    if (capacity > 0) {
        memory::IsolateAllocator allocator(isolate);

        list->objects = allocator.alloc<OObject *>(capacity * sizeof(void *));
        if (list->objects == nullptr) {
            isolate->gc->RawFree((OObject *) list, false);

            return {};
        }
    }

    O_GC_TRACK_RETURN(isolate, list, true);
}

HOObject orbiter::datatype::ListGet(List *list, bool *success, MSSize index) {
    *success = false;

    if (index < 0) {
        const auto old = list->length;

        index = (MSSize) old + index;

        if (index > old)
            return {};
    }

    if (index >= 0 && index < list->length) {
        *success = true;

        return HOObject(list->objects[index]);
    }

    return {};
}

HOType orbiter::datatype::ListTypeInit(Isolate *isolate) {
    auto list = MakeType(isolate, InstanceType::LIST, sizeof(List) - sizeof(OObject), 0, 0);
    return list;
}
