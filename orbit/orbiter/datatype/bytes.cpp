// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/bytes.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool BytesDtor(const Bytes *self) {
    if (self->shared != nullptr)
        support::SharedBufferRelease(O_GET_ISOLATE(self), self->shared);

    return true;
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::BytesTypeSetup(TypeInfo *self) {
    assert(self != nullptr);

    self->dtor = (DtorFn) BytesDtor;

    return true;
}

HOType orbiter::datatype::BytesTypeInit(Isolate *isolate) {
    return MakeType(isolate, nullptr, "Bytes", InstanceType::BYTES, 0, 0, 0);
}
