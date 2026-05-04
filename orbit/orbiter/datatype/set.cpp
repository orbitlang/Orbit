// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/result.h>

#include <orbit/orbiter/datatype/set.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool SetDtor(Set *self) {
    self->set.Finalize(nullptr);
    self->set.~ORHMap();

    self->lock.~AsyncRWLock();

    return true;
}

void SetTrace(const Set *self, const GCTraceCallback callback, const MSize epoch) {
    for (const auto *cursor = self->set.iter_begin; cursor != nullptr; cursor = cursor->iter_next)
        callback((OObject *) cursor->key, epoch);
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::SetTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) SetDtor;
    self->trace = (TraceFn) SetTrace;

    return true;
}

HOType orbiter::datatype::SetTypeInit(Isolate *isolate) {
    auto chan = MakeType(isolate, "Set", InstanceType::SET, 0, 0, 0);
    return chan;
}

HSet orbiter::datatype::SetNew(Isolate *isolate, const U32 size) {
    auto *set = MakeObject<Set>(isolate, InstanceType::SET);
    if (set != nullptr) {
        new(&set->set)ORHMap(isolate);

        const auto ok = size > 0 ? set->set.Initialize(size) : set->set.Initialize();
        if (!ok) {
            isolate->gc->RawFree((OObject *) set, false);

            return {};
        }

        new(&set->lock)sync::AsyncRWLock();
    }

    O_GC_TRACK_RETURN(isolate, set, true);
}
