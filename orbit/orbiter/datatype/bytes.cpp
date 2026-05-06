// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/bytes.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

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

HBytes orbiter::datatype::BytesNew(Isolate *isolate, const unsigned char *buffer, const MSize length,
                                   const bool frozen) {
    auto out = BytesNew(isolate, length, false);
    if (!out)
        return {};

    if (length > 0)
        memory::MemoryCopy(out->shared->buffer, buffer, length);

    out->shared->frozen = frozen;

    out->length = length;

    return out;
}

HBytes orbiter::datatype::BytesNew(Isolate *isolate, const MSize capacity, const bool frozen) {
    auto *bytes = MakeObject<Bytes>(isolate, InstanceType::BYTES);
    if (bytes == nullptr)
        return {};

    bytes->shared = support::SharedBufferNew(isolate, capacity, frozen);
    if (bytes->shared == nullptr) {
        isolate->gc->RawFree((OObject *) bytes, false);

        return {};
    }

    bytes->start = 0;
    bytes->length = 0;
    bytes->hash = 0;

    O_GC_TRACK_RETURN(isolate, bytes, false);
}

HBytes orbiter::datatype::BytesNew(const Bytes *src, const MSize start, const MSize length) {
    auto *isolate = O_GET_ISOLATE(src);

    if (start >= src->length || length >= src->length - start) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "slice bounds out of range for Bytes");

        return {};
    }

    auto *bytes = MakeObject<Bytes>(isolate, InstanceType::BYTES);
    if (bytes == nullptr)
        return {};

    bytes->shared = support::SharedBufferAcquire(src->shared);

    bytes->start = src->start + start;
    bytes->length = length;
    bytes->hash = 0;

    O_GC_TRACK_RETURN(isolate, bytes, false);
}

HOType orbiter::datatype::BytesTypeInit(Isolate *isolate) {
    return MakeType(isolate, nullptr, "Bytes", InstanceType::BYTES, 0, 0, 0);
}
