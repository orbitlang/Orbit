// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/util/hash.h>

#include <orbit/orbiter/datatype/support/byteops.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

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

bool CheckMutable(const Bytes *bytes) noexcept {
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
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// `==`: byte-wise equality. A non-Bytes operand is never equal.
static bool BytesOpEqual(const OObject *left, const OObject *right) {
    if (left == right)
        return true;

    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::BYTES))
        return false;

    return BytesEqual((const Bytes *) left, (const Bytes *) right);
}

/// `< / <= / > / >=`: lexicographic byte-wise comparison.
static int BytesOpCompare(const OObject *left, const OObject *right) {
    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::BYTES))
        return 0;

    return BytesCompare((const Bytes *) left, (const Bytes *) right);
}

/// `b in B`: substring containment. A non-Bytes needle raises TypeError.
static bool BytesOpContains(const OObject *container, const OObject *value, bool &result) {
    if (!O_IS_OBJECT(value) || !O_IS_TYPE(value, InstanceType::BYTES)) {
        ErrorSetWithObjType(O_GET_ISOLATE(container),
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::MISMATCH],
                            O_GET_TYPE(container)->name,
                            value);

        return false;
    }

    result = BytesContains((const Bytes *) container, (const Bytes *) value);

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — ARITHMETIC
// *********************************************************************************************************************

/// `a + b`: concatenation. Allocates a fresh non-frozen Bytes of size
/// `len(a) + len(b)` and copies both views into it. Both operands must be Bytes.
static bool BytesOpAdd(const OObject *left, const OObject *right, OObject *&result) {
    if (!O_IS_OBJECT(left) || !O_IS_TYPE(left, InstanceType::BYTES))
        return false;

    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::BYTES))
        return false;

    auto *l = (const Bytes *) left;
    auto *r = (const Bytes *) right;

    const auto out = BytesNew(O_GET_ISOLATE(left), l->length + r->length, false);
    if (!out)
        return false;

    if (l->length > 0) {
        std::shared_lock _(l->shared->rwlock);

        if (!BytesAppendData(out.get(), l->shared->buffer + l->start, l->length))
            return false;
    }

    if (r->length > 0) {
        std::shared_lock _(r->shared->rwlock);

        if (!BytesAppendData(out.get(), r->shared->buffer + r->start, r->length))
            return false;
    }

    result = (OObject *) out.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — INDEX
// *********************************************************************************************************************

/// `bytes[i]`: returns the byte at position `i` as a tagged SMI in the
/// range 0..255. Negative indices wrap from the end. Out of range raises IndexError;
/// non-integer index raises TypeError.
static bool BytesOpLoadIndex(const Bytes *self, const OObject *index, OObject *&result) {
    auto *isolate = O_GET_ISOLATE(self);

    IntegerUnderlying i;
    if (!NumberExtract(index, i)) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::MISMATCH],
                            isolate->primitive[(int) InstanceType::NUMBER]->name,
                            index);

        return false;
    }

    if (i < 0)
        i += (IntegerUnderlying) self->length;

    if (i < 0 || (MSize) i >= self->length) {
        ErrorSet(isolate,
                 IndexError::Details[IndexError::Reason::ID],
                 nullptr,
                 IndexError::Details[IndexError::Reason::OUT_OF_RANGE],
                 O_GET_TYPE(self)->name,
                 (long long) i,
                 (long long) self->length);

        return false;
    }

    std::shared_lock _(self->shared->rwlock);

    const auto byte = self->shared->buffer[self->start + (MSize) i];

    result = (OObject *) O_TO_SMI((MSSize) byte);

    return true;
}

/// `bytes[i] = v`: writes a byte (Int in range 0..255) at position `i`.
/// Negative indices wrap from the end. Out-of-range index raises
/// IndexError; non-integer index/value raises TypeError; out-of-range
/// value or frozen buffer raises ValueError. Invalidates the cached hash.
static bool BytesOpStoreIndex(Bytes *self, const OObject *index, const OObject *value) {
    auto *isolate = O_GET_ISOLATE(self);

    IntegerUnderlying i;
    if (!NumberExtract(index, i)) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::MISMATCH],
                            isolate->primitive[(int) InstanceType::NUMBER]->name,
                            index);

        return false;
    }

    IntegerUnderlying v;
    if (!NumberExtract(value, v)) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::MISMATCH],
                            isolate->primitive[(int) InstanceType::NUMBER]->name,
                            value);

        return false;
    }

    if (v < 0 || v > 255) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "byte value must be in [0, 255]");

        return false;
    }

    if (i < 0)
        i += (IntegerUnderlying) self->length;

    if (i < 0 || (MSize) i >= self->length) {
        ErrorSet(isolate,
                 IndexError::Details[IndexError::Reason::ID],
                 nullptr,
                 IndexError::Details[IndexError::Reason::OUT_OF_RANGE],
                 O_GET_TYPE(self)->name,
                 (long long) i,
                 (long long) self->length);

        return false;
    }

    if (!CheckMutable(self))
        return false;

    std::unique_lock _(self->shared->rwlock);

    self->shared->buffer[self->start + (MSize) i] = (unsigned char) v;

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A non-empty Bytes is truthy.
static bool BytesOpToBool(const OObject *self) {
    return ((const Bytes *) self)->length != 0;
}

/// Render as `b"..."` with non-printable / non-ASCII bytes shown as `\xHH`.
static OObject *BytesOpToString(orbiter::Isolate *isolate, const Bytes *self) {
    StringBuilder builder(isolate);

    constexpr unsigned char prefix[] = {'b', '"'};
    constexpr unsigned char close_quote[] = {'"'};

    // Hint: worst case is 4 bytes per source byte ("\xHH"), plus the
    // two-byte prefix and one-byte closing quote.
    if (!builder.Write(prefix, 2, self->length * 4 + 3))
        return nullptr;

    std::shared_lock _(self->shared->rwlock);

    if (!builder.WriteEscaped(self->shared->buffer + self->start, self->length, 1))
        return nullptr;

    if (!builder.Write(close_quote, 1, 0))
        return nullptr;

    return (OObject *) ORStringNew(isolate, builder).get();
}

// *********************************************************************************************************************
// TYPE OPS — RUNTIME
// *********************************************************************************************************************

/// FNV-1 hash over the view's bytes. Only frozen Bytes are hashable —
/// using a mutable Bytes as a Dict/Set key is forbidden because any
/// subsequent mutation would silently invalidate its position in the
/// hashmap.
///
/// Returning `HASH_ERROR` for a non-frozen Bytes lets the dispatcher in
/// `oops.cpp` synthesise the canonical `TypeError("unhashable type:
/// 'Bytes'")` message — no need to set the panic explicitly here.
///
/// On a frozen Bytes the result is cached on `bytes->hash`; computed
/// hashes that would otherwise equal 0 or HASH_ERROR are coerced to 1
/// — same convention used by ORString — so 0 unambiguously means
/// "uncached" and HASH_ERROR is reserved for the unhashable signal.
/// The cache stays valid for the lifetime of the object: freezing is
/// monotonic.
static MSize BytesOpHash(const OObject *self) {
    auto *bytes = (Bytes *) self;

    if (!bytes->shared->IsFrozen())
        return HASH_ERROR;

    if (bytes->hash != 0)
        return bytes->hash;

    std::shared_lock _(bytes->shared->rwlock);

    auto h = fnv1_hash(bytes->shared->buffer + bytes->start, bytes->length);
    if (h == 0 || h == HASH_ERROR)
        h = 1;

    bytes->hash = h;

    return h;
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::BytesAppend(Bytes *bytes, const Bytes *other) noexcept {
    std::shared_lock a_lock(bytes->shared->rwlock, std::defer_lock);
    std::shared_lock b_lock(other->shared->rwlock, std::defer_lock);

    auto *isolate = O_GET_ISOLATE(bytes);

    if (!CheckMutable(bytes))
        return false;

    LockTwoShared(bytes->shared, other->shared, a_lock, b_lock);

    const auto *other_buffer = other->shared->buffer;
    if (bytes->shared == other->shared) {
        // If destination and source are the same, make an initial call to reallocate the buffer (if necessary)
        // and update the buffer pointer to avoid dangling pointer scenarios
        if (!support::SharedBufferAppendLocked(isolate, bytes->shared, nullptr, bytes->start + bytes->length,
                                               other->length))
            return false;

        other_buffer = other->shared->buffer;
    }

    if (other->length == 0)
        return true;

    if (!support::SharedBufferAppendLocked(O_GET_ISOLATE(bytes), bytes->shared, other_buffer + other->start,
                                           bytes->start + bytes->length, other->length))
        return false;

    bytes->length += other->length;

    return true;
}

bool orbiter::datatype::BytesAppendData(Bytes *bytes, const unsigned char *buffer, const MSize length) noexcept {
    if (!CheckMutable(bytes))
        return false;

    if (length == 0)
        return true;

    if (!support::SharedBufferAppend(O_GET_ISOLATE(bytes), bytes->shared, buffer, bytes->start + bytes->length, length))
        return false;

    bytes->length += length;

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

    auto &ops = ((TypeInfoOps *) self)->ops;

    // --- Comparison ---
    ops.equal = BytesOpEqual;
    ops.compare = BytesOpCompare;
    ops.contains = BytesOpContains;

    // --- Arithmetic ---
    ops.add = BytesOpAdd;

    // --- Index ---
    ops.load_index = (BinaryFn) BytesOpLoadIndex;
    ops.store_index = (TernaryFn) BytesOpStoreIndex;

    // --- Conversion ---
    ops.to_bool = BytesOpToBool;
    ops.to_string = (ToStrFn) BytesOpToString;

    // --- Runtime ---
    ops.hash = BytesOpHash;

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

    if (start > src->length || length > src->length - start) {
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
    return MakeType(isolate, "Bytes", InstanceType::BYTES, sizeof(Bytes) - sizeof(OObject), 0, 0);
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
                           needle->shared->buffer + needle->start, needle->length);
}
