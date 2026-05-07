// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/orbiter/datatype/support/byteops.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

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

bool IsFrozen(const Bytes *bytes) noexcept {
    if (bytes->shared->IsFrozen()) {
        ErrorSet(O_GET_ISOLATE(bytes),
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "cannot modify frozen Bytes object");

        return false;
    }

    return true;
}

/// Internal: take shared locks on both `a->rwlock` and `b->rwlock` in a
/// deadlock-free order. When the two SharedBuffers are the same, only
/// one lock is acquired. Returns the two unique_lock-style guards via
/// the std::shared_lock RAII; the caller binds the result.
///
/// (Helper exists because every read-side comparison/search needs the
/// exact same prelude, and pointer-ordered locking is the standard
/// idiom to avoid deadlock with another pair locking in reverse order.)
static void LockTwoShared(support::SharedBuffer *a, support::SharedBuffer *b,
                          std::shared_lock<orbiter::sync::AsyncRWLock> &out_a,
                          std::shared_lock<orbiter::sync::AsyncRWLock> &out_b) {
    if (a == b) {
        out_a = std::shared_lock(a->rwlock);
        return;
    }

    if (a < b) {
        out_a = std::shared_lock(a->rwlock);
        out_b = std::shared_lock(b->rwlock);
        return;
    }

    out_b = std::shared_lock(b->rwlock);
    out_a = std::shared_lock(a->rwlock);
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::BytesAppend(Bytes *bytes, const Bytes *other) noexcept {
    std::shared_lock a_lock(bytes->shared->rwlock, std::defer_lock);
    std::shared_lock b_lock(other->shared->rwlock, std::defer_lock);

    if (!IsFrozen(bytes))
        return false;

    LockTwoShared(bytes->shared, other->shared, a_lock, b_lock);

    if (other->length == 0)
        return true;

    if (!support::SharedBufferAppend(O_GET_ISOLATE(bytes), bytes->shared, other->shared->buffer,
                                     bytes->start + bytes->length, other->length))
        return false;

    bytes->length += other->length;
    bytes->hash = 0;

    return true;
}

bool orbiter::datatype::BytesAppendData(Bytes *bytes, const unsigned char *buffer, const MSize length) noexcept {
    if (!IsFrozen(bytes))
        return false;

    if (length == 0)
        return true;

    if (!support::SharedBufferAppend(O_GET_ISOLATE(bytes), bytes->shared, buffer, bytes->start + length, length))
        return false;

    bytes->length += length;
    bytes->hash = 0;

    return true;
}

bool orbiter::datatype::BytesContains(const Bytes *haystack, const Bytes *needle) noexcept {
    return BytesFind(haystack, needle, 0) >= 0;
}

bool orbiter::datatype::BytesEqual(const Bytes *left, const Bytes *right) noexcept {
    std::shared_lock l_lock(left->shared->rwlock, std::defer_lock);
    std::shared_lock r_lock(right->shared->rwlock, std::defer_lock);

    if (left == right)
        return true;

    if (left->length != right->length)
        return false;

    // Same buffer, same window → same bytes.
    if (left->shared == right->shared && left->start == right->start)
        return true;

    if (left->length == 0)
        return true;

    LockTwoShared(left->shared, right->shared, l_lock, r_lock);

    return memcmp(left->shared->buffer + left->start, right->shared->buffer + right->start, left->length) == 0;
}

bool orbiter::datatype::BytesTypeSetup(TypeInfo *self) noexcept {
    assert(self != nullptr);

    self->dtor = (DtorFn) BytesDtor;

    return true;
}

HBytes orbiter::datatype::BytesNew(Isolate *isolate, const unsigned char *buffer, const MSize length,
                                   const bool frozen) noexcept {
    auto out = BytesNew(isolate, length, false);
    if (!out)
        return {};

    if (length > 0)
        memory::MemoryCopy(out->shared->buffer, buffer, length);

    out->shared->frozen = frozen;

    out->length = length;

    return out;
}

HBytes orbiter::datatype::BytesNew(Isolate *isolate, const MSize capacity, const bool frozen) noexcept {
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

HBytes orbiter::datatype::BytesNew(const Bytes *src, const MSize start, const MSize length) noexcept {
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

int orbiter::datatype::BytesCompare(const Bytes *left, const Bytes *right) noexcept {
    std::shared_lock a_lock(left->shared->rwlock, std::defer_lock);
    std::shared_lock b_lock(right->shared->rwlock, std::defer_lock);

    if (left == right)
        return 0;

    if (left->shared == right->shared && left->start == right->start && left->length == right->length)
        return 0;

    LockTwoShared(left->shared, right->shared, a_lock, b_lock);

    const auto common = left->length < right->length ? left->length : right->length;
    if (common > 0) {
        const auto cmp = memcmp(left->shared->buffer + left->start, right->shared->buffer + right->start, common);
        if (cmp != 0)
            return cmp;
    }

    if (left->length < right->length)
        return -1;

    if (left->length > right->length)
        return 1;

    return 0;
}

MSSize orbiter::datatype::BytesFind(const Bytes *haystack, const Bytes *needle, const MSize start) noexcept {
    std::shared_lock h_lock(haystack->shared->rwlock, std::defer_lock);
    std::shared_lock n_lock(needle->shared->rwlock, std::defer_lock);

    if (needle->length == 0)
        return start <= haystack->length ? (MSSize) start : -1;

    if (needle->length > haystack->length)
        return -1;

    if (start > haystack->length - needle->length)
        return -1;

    LockTwoShared(haystack->shared, needle->shared, h_lock, n_lock);

    return support::Search(haystack->shared->buffer + haystack->start + start, haystack->length - start,
                           needle->shared->buffer, needle->length);
}
