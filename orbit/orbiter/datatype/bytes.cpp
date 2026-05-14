// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/util/hash.h>

#include <orbit/orbiter/datatype/support/byteops.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/orbiter/datatype/bytes.h>

#include "support/common.h"

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

/// Default whitespace bytes for strip / lstrip / rstrip when no `chars`
/// is supplied: space, tab, LF, CR, VT, FF — same set as Python's
/// default.
constexpr unsigned char kDefaultStripChars[] = {0x20, 0x09, 0x0A, 0x0D, 0x0B, 0x0C};
constexpr MSize kDefaultStripCharsLen = sizeof(kDefaultStripChars);

/// Linear membership test over a small `chars` set. The expected size
/// is single-digit (whitespace defaults are 6 bytes), so a tight loop
/// beats any hashed-set approach.
static bool ByteInSet(const unsigned char b, const unsigned char *chars, const MSize len) {
    for (MSize i = 0; i < len; i++) {
        if (chars[i] == b)
            return true;
    }

    return false;
}

/// Extract a single-byte value (`Int` in `[0, 255]`) from @p value into @p out.
static bool ExtractByte(orbiter::Isolate *isolate, const OObject *value, unsigned char *out) noexcept {
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

    *out = (unsigned char) v;

    return true;
}

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

bool BytesReplace(Bytes *self, const Bytes *pattern, const Bytes *sub, const MSSize count) noexcept {
    std::unique_lock self_lock(self->shared->rwlock);
    std::shared_lock sub_lock(sub->shared->rwlock);
    std::shared_lock p_lock(pattern->shared->rwlock);

    if (!CheckMutable(self))
        return false;

    auto self_buf = self->shared->buffer + self->start;
    auto self_end = self_buf + self->length;

    const auto sub_buf = sub->shared->buffer + sub->start;
    const auto p_buf = pattern->shared->buffer + pattern->start;

    const auto sub_length = sub->length;
    const auto pattern_length = pattern->length;

    const auto r_count = support::Count(self_buf, self->length, p_buf, pattern_length, count);
    if (r_count == 0)
        return true;

    const auto delta = (int) (sub_length * r_count) - (int) (pattern_length * r_count);

    auto found = support::Search(self_buf, self->length, p_buf, pattern_length);
    if (delta <= 0) {
        auto last_end = (unsigned char *) nullptr;

        while (found >= 0) {
            if (last_end != nullptr) {
                orbiter::memory::MemoryCopy(last_end, self_buf, self_end - self_buf);

                found -= (MSSize) pattern_length - (MSSize) sub_length;
            }

            orbiter::memory::MemoryCopy(self_buf + found, sub_buf, sub_length);

            last_end = self_buf + found + sub_length;

            self_buf += found + pattern_length;

            found = support::Search(self_buf, self_end - self_buf, p_buf, pattern_length);
        }

        if (last_end != nullptr)
            orbiter::memory::MemoryCopy(last_end, self_buf, self_end - self_buf);

        self->length += (MSize) delta;

        return true;
    }

    // Grow buffer (if necessary)
    if (!support::SharedBufferEnlargeLocked(O_GET_ISOLATE(self), self->shared, self->start + self->length + delta))
        return false;

    self->length += (MSize) delta;

    self_buf = self->shared->buffer + self->start;
    self_end = self_buf + self->length;

    while (found >= 0) {
        std::memmove(self_buf + found + sub_length, self_buf + found + pattern_length,
                     self_end - (self_buf + found + pattern_length));

        orbiter::memory::MemoryCopy(self_buf + found, sub_buf, sub_length);

        self_buf += found + sub_length;

        found = support::Search(self_buf, self_end - self_buf, p_buf, pattern_length);
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

static void LockTwoSelfUnique(support::SharedBuffer *a, support::SharedBuffer *b,
                              std::unique_lock<orbiter::sync::AsyncRWLock> &out_a,
                              std::shared_lock<orbiter::sync::AsyncRWLock> &out_b) {
    if (a == b) {
        out_a = std::unique_lock(a->rwlock);

        return;
    }

    if (a < b) {
        out_a = std::unique_lock(a->rwlock);
        out_b = std::shared_lock(b->rwlock);

        return;
    }

    out_b = std::shared_lock(b->rwlock);
    out_a = std::unique_lock(a->rwlock);
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

    unsigned char byte;
    if (!ExtractByte(isolate, value, &byte))
        return false;

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

    self->shared->buffer[self->start + (MSize) i] = byte;

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
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_FUNCTION(bytes_create, create,
                 R"DOC(
@brief Create a new Bytes.

The optional `init` argument shapes the result:
  - omitted      → an empty mutable Bytes.
  - Int N >= 0   → a mutable Bytes of `N` zeroed bytes.
  - Bytes b      → an independent copy of `b`'s contents (mutable, never
                   shares the SharedBuffer with `b`).
  - List/Tuple l → List/Tuple of number between [0, 255].
  - String s     → a mutable Bytes containing the UTF-8 bytes of `s`.

@param init?  Optional seed (Int, Bytes, List, Tuple or String).

@return A new mutable Bytes.

@panic TypeError   When `init` is not an Int, Bytes, List, Tuple String or nil.
@panic ValueError  When `init` is a negative Int.

@see freeze

@example
    Bytes()                      // mutable, length 0
    Bytes(init=8)                // mutable, 8 zero bytes
    Bytes(init=other)            // mutable copy
    Bytes(init="hello")          // UTF-8 bytes of "hello"
    Bytes(init=[0x10,0x34,0x11]) // list of bytes
)DOC", 0, "init", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("init", true,
                       InstanceType::NUMBER,
                       InstanceType::BYTES,
                       InstanceType::LIST,
                       InstanceType::STRING,
                       InstanceType::TUPLE));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    if (O_IS_SENTINEL(argv[0])) {
        const auto out = BytesNew(isolate, (MSize) 0, false);
        if (!out)
            return {};

        return HOObject((OObject *) out.get());
    }

    if (O_IS_TYPE(argv[0], InstanceType::NUMBER)) {
        IntegerUnderlying n;
        if (!NumberExtract(argv[0], n))
            return {};

        if (n < 0) {
            ErrorSet(isolate,
                     ValueError::Details[ValueError::Reason::ID],
                     nullptr,
                     "Bytes capacity cannot be negative");

            return {};
        }

        const auto out = BytesNew(isolate, (MSize) n, false);
        if (!out)
            return {};

        return HOObject((OObject *) out.get());
    }

    const auto out = BytesNew(isolate, argv[0]);
    if (!out)
        return {};

    return HOObject((OObject *) out.get());
}

RUNTIME_METHOD(bytes_append, append,
               R"DOC(
@brief Append a single byte to the buffer in place.

@param value  The byte to append. Must be an Int in `[0, 255]`.

@return Self.

@panic TypeError   When `value` is not an Int.
@panic ValueError  When `value` is out of `[0, 255]` or the Bytes is frozen.

@see clear, extend

@example
    let b = Bytes()
    b.append(0x41)
    b.length()      // 1
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *self = (Bytes *) argv[0];

    unsigned char byte;
    if (!ExtractByte(O_GET_ISOLATE(self), argv[1], &byte))
        return {};

    if (!BytesAppendData(self, &byte, 1))
        return {};

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_clear, clear,
               R"DOC(
@brief Reset the buffer to length 0 in place.

The underlying SharedBuffer's capacity is retained for reuse — only the
view's length is set back to 0. Co-owners of the same SharedBuffer are
not affected.

@return Self.

@panic ValueError  When the Bytes is frozen.

@see length

@example
    let b = Bytes(b"\x01\x02\x03")
    b.clear()
    b.length()      // 0
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    auto *self = (Bytes *) argv[0];

    // This is a 'soft' check: even if it were to change while we're setting the length to zero,
    // it would only affect this view.
    if (!CheckMutable(self))
        return {};

    self->length = 0;

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_compact, compact,
               R"DOC(
@brief Reclaim wasted space at the front of the underlying buffer.

When self has been created as a slice or had its `start` advanced by
`lstrip` / `strip`, the bytes before `start` remain allocated in the
SharedBuffer but are no longer visible through self. For short-lived
Bytes this is harmless, but a long-lived Bytes that accumulates many
prefix-trimming operations can keep growing its wasted prefix.
`compact` physically shifts the visible bytes back to the start of the
SharedBuffer (`start = 0`) so that prefix is reclaimed.

This method modifies **the underlying SharedBuffer**, not just self's
view indices: every byte before `start` is overwritten by the shift.
For that reason it requires self to be the **sole owner** of its
SharedBuffer — co-owners (typically slices created with `b[a:c]` or
`split`) would otherwise see their bytes silently corrupted, since
their `(start, length)` indices into the now-shifted buffer no longer
point at the same content.

The visible byte sequence of self is unchanged after the call; only
its position in the underlying memory layout moves. The SharedBuffer's
capacity is **not** reduced — only the slack at the front is recycled
into available append space at the end.

Use it when you've narrowed a long-lived buffer with `lstrip` / `strip`
and want subsequent `append` / `extend` calls to reuse the freed
prefix space instead of growing the buffer further.

@return Self.

@panic ValueError  When the Bytes is frozen, or when its SharedBuffer
                   has co-owners (slices, split pieces, …).

@see lstrip, strip, copy

@example
    let b = Bytes(b"   hello")
    b.lstrip()      // start=3, length=5, buffer still 8 bytes long
    b.compact()     // start=0, length=5, prefix reclaimed
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    auto *self = (Bytes *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    std::unique_lock lock(self->shared->rwlock);

    if (!CheckMutable(self))
        return {};

    if (self->shared->counter.load(std::memory_order_relaxed) > 1) {
        lock.unlock();

        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "cannot compact a Bytes with co-owners; clone first");

        return {};
    }

    // Already compacted: nothing to do.
    if (self->start == 0)
        return HOObject(argv[0]);

    // memmove handles the overlapping case (start < length) safely.
    if (self->length > 0)
        memmove(self->shared->buffer, self->shared->buffer + self->start, self->length);

    self->start = 0;

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_contains, contains,
               R"DOC(
@brief Return true if the buffer contains the given subrange.

An empty needle is contained in every Bytes.

@param sub  The byte sequence to search for.

@return true if `sub` is a contiguous subrange of self, false otherwise.

@panic TypeError  When `sub` is not a Bytes.

@see count, find

@example
    b"banana".contains(b"ana")    // true
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("sub", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    return HOObject((OObject *) BOOL_TO_OBOOL(BytesContains((Bytes *) argv[0], (Bytes *) argv[1])));
}

RUNTIME_METHOD(bytes_count, count,
               R"DOC(
@brief Return the number of non-overlapping occurrences of `sub` in self.

Returns 0 when `sub` is empty.

@param sub  The byte sequence to count.

@return A non-negative Int.

@panic TypeError  When `sub` is not a Bytes.

@see contains, find

@example
    b"banana".count(b"an")    // 2
    b"aaaa".count(b"aa")      // 2  (non-overlapping)
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("sub", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];
    const auto *sub = (Bytes *) argv[1];

    IntegerUnderlying n = 0;

    if (sub->length > 0) {
        std::shared_lock<orbiter::sync::AsyncRWLock> s_lock;
        std::shared_lock<orbiter::sync::AsyncRWLock> u_lock;
        LockTwoShared(self->shared, sub->shared, s_lock, u_lock);

        n = (IntegerUnderlying) support::Count(self->shared->buffer + self->start, self->length,
                                               sub->shared->buffer + sub->start, sub->length,
                                               -1);
    }

    auto result = IntNew(O_GET_ISOLATE(self), n);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(bytes_ends_with, ends_with,
               R"DOC(
@brief Return true if self ends with the given suffix.

@param suffix  The byte sequence to test.

@return true when self's last `len(suffix)` bytes equal `suffix`,
        false otherwise. An empty suffix returns true.

@panic TypeError  When `suffix` is not a Bytes.

@see starts_with, find

@example
    b"hello.txt".ends_with(b".txt")    // true
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("suffix", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];
    const auto *suffix = (Bytes *) argv[1];

    if (suffix->length > self->length)
        return HOObject((OObject *) BOOL_TO_OBOOL(false));

    if (suffix->length == 0)
        return HOObject((OObject *) BOOL_TO_OBOOL(true));

    std::shared_lock<orbiter::sync::AsyncRWLock> s_lock;
    std::shared_lock<orbiter::sync::AsyncRWLock> p_lock;
    LockTwoShared(self->shared, suffix->shared, s_lock, p_lock);

    const auto eq = memcmp(self->shared->buffer + self->start + self->length - suffix->length,
                           suffix->shared->buffer + suffix->start,
                           suffix->length) == 0;

    return HOObject((OObject *) BOOL_TO_OBOOL(eq));
}

RUNTIME_METHOD(bytes_extend, extend,
               R"DOC(
@brief Append every byte of `other` to self in place.

@return Self.

@param other  The byte sequence to append.

@panic TypeError   When `other` is not a Bytes.
@panic ValueError  When the Bytes is frozen.

@see append

@example
    let b = Bytes(b"\x01\x02")
    b.extend(b"\x03\x04")
    b.length()      // 4
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("other", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    if (!BytesAppend((Bytes *) argv[0], (const Bytes *) argv[1]))
        return {};

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_find, find,
               R"DOC(
@brief Return the byte offset of the first occurrence of `sub` in self.

Searches forward from `start`. Returns -1 if not found. An empty `sub`
returns `start` (or -1 if `start` is past the end of self).

@param sub     The byte sequence to search for.
@param start?  Byte offset at which the search begins. Defaults to 0.

@return Index of the first match, or -1 if not found.

@panic TypeError   When `sub` is not a Bytes or `start` is not an Int.
@panic ValueError  When `start` is negative.

@see rfind, contains, count

@example
    b"hello".find(b"ll")     //  2
    b"hello".find(b"xy")     // -1
)DOC", 2, "start", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("sub", false, InstanceType::BYTES),
                   PCHECK_DEF("start", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];
    const auto *sub = (Bytes *) argv[1];
    auto *isolate = O_GET_ISOLATE(self);

    IntegerUnderlying start = 0;
    if (!O_IS_SENTINEL(argv[2])) {
        if (!NumberExtract(argv[2], start))
            return {};

        if (start < 0) {
            ErrorSet(isolate,
                     ValueError::Details[ValueError::Reason::ID],
                     nullptr,
                     "start index cannot be negative");

            return {};
        }
    }

    const auto idx = BytesFind(self, sub, start);

    auto result = IntNew(isolate, idx);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(bytes_freeze, freeze,
               R"DOC(
@brief Freeze the buffer so no further mutation is possible.

When self is the only owner of its SharedBuffer, the freeze is applied
in place and self itself is returned. Otherwise an independent frozen
copy is allocated and returned, leaving the original untouched.

Once frozen, any `append` / `extend` / `clear` / index-store on the
returned Bytes raises a ValueError. The Bytes also becomes hashable
(see `is_frozen`).

@return Either self (frozen in place) or a new frozen Bytes with the
        same contents.

@see is_frozen

@example
    let a = Bytes(b"\x01\x02\x03")
    let b = a.freeze()
    b.is_frozen()    // true
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    auto *self = (Bytes *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    std::unique_lock lock(self->shared->rwlock);

    if (self->shared->counter.load(std::memory_order_relaxed) == 1) {
        support::SharedBufferFreezeLocked(self->shared);

        return HOObject((OObject *) self);
    }

    lock.unlock();

    // Multi-owner: allocate a private frozen copy.
    const auto out = BytesNew(isolate, argv[0]);

    return HOObject((OObject *) out.get());
}

RUNTIME_METHOD(bytes_hex, hex,
               R"DOC(
@brief Return the lowercase hexadecimal representation of the buffer.

Each byte produces exactly two hex digits; the result has length `2 * len(self)`.

@return A new String of lowercase hex digits.

@see is_ascii

@example
    b"\x48\x69".hex()    // "4869"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    StringBuilder builder(isolate);

    std::shared_lock _(self->shared->rwlock);

    if (!builder.WriteHex(self->shared->buffer + self->start, self->length))
        return {};

    return HOObject((OObject *) ORStringNew(isolate, builder).get());
}

RUNTIME_METHOD(bytes_is_ascii, is_ascii,
               R"DOC(
@brief Return true if every byte is in the ASCII range [0, 127].

An empty Bytes is considered ASCII.

@return true when all bytes are < 0x80, false otherwise.

@see hex

@example
    b"hello".is_ascii()       // true
    b"\xc3\xa8".is_ascii()    // false  (UTF-8 'è')
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];

    std::shared_lock _(self->shared->rwlock);

    for (MSize i = 0; i < self->length; i++) {
        if (self->shared->buffer[self->start + i] >= 0x80)
            return HOObject((OObject *) BOOL_TO_OBOOL(false));
    }

    return HOObject((OObject *) BOOL_TO_OBOOL(true));
}

RUNTIME_METHOD(bytes_is_frozen, is_frozen,
               R"DOC(
@brief Return true if the underlying buffer has been frozen.

@return true when frozen, false otherwise.

@see freeze

@example
    let b = Bytes(4)
    b.is_frozen()        // false
    let f = b.freeze()
    f.is_frozen()        // true
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    return HOObject((OObject *) BOOL_TO_OBOOL(((const Bytes *) argv[0])->shared->IsFrozen()));
}

RUNTIME_METHOD(bytes_length, length,
               R"DOC(
@brief Return the number of bytes in the view.

@return A non-negative Int.

@see clear

@example
    b"hello".length()    // 5
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];

    auto result = IntNew(O_GET_ISOLATE(self), (IntegerUnderlying) self->length);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(bytes_lower, lower,
               R"DOC(
@brief Lowercase every ASCII uppercase byte in place.

Bytes outside `[0x41, 0x5A]` ('A'..'Z') are left untouched — non-ASCII
content is preserved bit-for-bit. To obtain a lowercased copy without
modifying self, call `copy()` first.

@return Self.

@panic ValueError  When the Bytes is frozen.

@see upper, copy

@example
    let b = Bytes(b"Hello")
    b.lower()
    b               // b"hello"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];

    std::unique_lock _(self->shared->rwlock);

    if (!CheckMutable(self))
        return {};


    auto *buf = self->shared->buffer + self->start;
    for (MSize i = 0; i < self->length; i++) {
        const auto b = buf[i];

        if (b >= 'A' && b <= 'Z')
            buf[i] = (unsigned char) (b + 0x20);
    }

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_lstrip, lstrip,
               R"DOC(
@brief Remove leading bytes in `chars` from self in place.

The view's `start` is bumped past the stripped bytes and `length`
shrunk accordingly — no allocation, no buffer copy. Co-owners of the
SharedBuffer are unaffected (their own `(start, length)` is independent).

When `chars` is omitted, the default whitespace set is used:
`{0x20, 0x09, 0x0A, 0x0D, 0x0B, 0x0C}` (space, tab, LF, CR, VT, FF).

@param chars?  Set of bytes to strip. Defaults to ASCII whitespace.

@return Self.

@panic TypeError   When `chars` is not a Bytes.
@panic ValueError  When the Bytes is frozen.

@see strip, rstrip

@example
    let b = Bytes(b"   hello")
    b.lstrip()
    b               // b"hello"
)DOC", 1, "chars", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("chars", true, InstanceType::BYTES));
    PCHECK_CHECK(params);

    auto *self = (Bytes *) argv[0];
    const auto *chars_b = O_IS_SENTINEL(argv[1]) ? nullptr : (const Bytes *) argv[1];

    std::shared_lock<orbiter::sync::AsyncRWLock> s_lock;
    std::shared_lock<orbiter::sync::AsyncRWLock> c_lock;
    if (chars_b != nullptr)
        LockTwoShared(self->shared, chars_b->shared, s_lock, c_lock);
    else
        s_lock = std::shared_lock(self->shared->rwlock);

    if (!CheckMutable(self))
        return {};

    const unsigned char *cset = chars_b != nullptr ? chars_b->shared->buffer + chars_b->start : kDefaultStripChars;
    const MSize cset_len = chars_b != nullptr ? chars_b->length : kDefaultStripCharsLen;

    MSize i = 0;
    while (i < self->length && ByteInSet(self->shared->buffer[self->start + i], cset, cset_len))
        i++;

    self->start += i;
    self->length -= i;

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_replace, replace,
               R"DOC(
@brief Replace occurrences of `old` with `new` in place.

Replaces up to `max` non-overlapping matches scanned left-to-right.
When `max` is omitted (or -1), every occurrence is replaced. The view
shrinks or grows accordingly: shrink leaves a slack tail at the end of
the underlying buffer (use `compact` to reclaim it), expand extends
the view to the right and may overwrite bytes observed by co-owners
sitting immediately after self — same semantics as `extend`.

@param old   Byte sequence to search for. Must not be empty.
@param new   Byte sequence to insert in place of each match.
@param max?  Maximum number of replacements. Defaults to "all".

@return Self.

@panic TypeError   When `old`, `new` or `max` have the wrong type.
@panic ValueError  When `old` is empty.

@see find, count, compact

@example
    let b = Bytes(b"ababab")
    b.replace(b"a", b"X")
    b               // b"XbXbXb"
)DOC", 3, "max", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("old", false, InstanceType::BYTES),
                   PCHECK_DEF("new", false, InstanceType::BYTES),
                   PCHECK_DEF("max", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    MSSize max = -1;
    if (!O_IS_SENTINEL(argv[3])) {
        IntegerUnderlying raw;
        if (!NumberExtract(argv[3], raw))
            return {};

        max = (MSSize) raw;
    }

    if (!BytesReplace((Bytes *) argv[0], (const Bytes *) argv[1], (const Bytes *) argv[2], max))
        return {};

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_rfind, rfind,
               R"DOC(
@brief Return the byte offset of the last occurrence of `sub` in self.

Searches the slice `self[0:end]`. Returns -1 if not found. An empty
`sub` returns `end` (which defaults to `length`).

@param sub   The byte sequence to search for.
@param end?  Exclusive upper bound of the search window. Defaults to
             the length of self.

@return Index of the last match within the window, or -1 if not found.

@panic TypeError   When `sub` is not a Bytes or `end` is not an Int.
@panic ValueError  When `end` is negative.

@see find, contains

@example
    b"abcabc".rfind(b"b")    // 4
    b"abc".rfind(b"z")       // -1
)DOC", 2, "end", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("sub", false, InstanceType::BYTES),
                   PCHECK_DEF("end", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];
    const auto *sub = (Bytes *) argv[1];
    auto *isolate = O_GET_ISOLATE(self);

    auto end = (IntegerUnderlying) self->length;
    if (!O_IS_SENTINEL(argv[2])) {
        if (!NumberExtract(argv[2], end))
            return {};

        if (end < 0) {
            ErrorSet(isolate,
                     ValueError::Details[ValueError::Reason::ID],
                     nullptr,
                     "end index cannot be negative");

            return {};
        }

        if ((MSize) end > self->length)
            end = (IntegerUnderlying) self->length;
    }

    if (sub->length == 0)
        return HOObject((OObject *) IntNew(isolate, end).get());

    if (sub->length > (MSize) end) {
        const auto miss = IntNew(isolate, -1);
        if (!miss)
            return {};

        return HOObject((OObject *) miss.get());
    }

    std::shared_lock<orbiter::sync::AsyncRWLock> s_lock;
    std::shared_lock<orbiter::sync::AsyncRWLock> p_lock;
    LockTwoShared(self->shared, sub->shared, s_lock, p_lock);

    const auto result = (IntegerUnderlying) support::RSearch(self->shared->buffer + self->start, self->length,
                                                             sub->shared->buffer + sub->start, sub->length);

    auto out = IntNew(isolate, result);
    if (!out)
        return {};

    return HOObject(std::move(out));
}

RUNTIME_METHOD(bytes_rstrip, rstrip,
               R"DOC(
@brief Remove trailing bytes in `chars` from self in place.

The view's `length` is shrunk to exclude the stripped suffix — no
allocation, no buffer copy. Co-owners of the SharedBuffer are unaffected.

When `chars` is omitted, the default whitespace set is used (see `lstrip`).

@param chars?  Set of bytes to strip. Defaults to ASCII whitespace.

@return Self.

@panic TypeError   When `chars` is not a Bytes.
@panic ValueError  When the Bytes is frozen.

@see strip, lstrip

@example
    let b = Bytes(b"hello   ")
    b.rstrip()
    b               // b"hello"
)DOC", 1, "chars", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("chars", true, InstanceType::BYTES));
    PCHECK_CHECK(params);

    auto *self = (Bytes *) argv[0];
    const auto *chars_b = O_IS_SENTINEL(argv[1]) ? nullptr : (const Bytes *) argv[1];

    std::shared_lock<orbiter::sync::AsyncRWLock> s_lock;
    std::shared_lock<orbiter::sync::AsyncRWLock> c_lock;
    if (chars_b != nullptr)
        LockTwoShared(self->shared, chars_b->shared, s_lock, c_lock);
    else
        s_lock = std::shared_lock(self->shared->rwlock);

    if (!CheckMutable(self))
        return {};

    const unsigned char *cset = chars_b != nullptr ? chars_b->shared->buffer + chars_b->start : kDefaultStripChars;
    const MSize cset_len = chars_b != nullptr ? chars_b->length : kDefaultStripCharsLen;

    MSize end = self->length;
    while (end > 0 && ByteInSet(self->shared->buffer[self->start + end - 1], cset, cset_len))
        end--;

    self->length = end;

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_split, split,
               R"DOC(
@brief Split self into a List of Bytes around `sep`.

Returns the substrings produced by walking self left-to-right and
splitting at each non-overlapping occurrence of `sep`. The pieces are
**zero-copy slices** sharing self's SharedBuffer. When `max` is
provided, at most `max` splits are performed: the remainder of self is
returned as the final element. -1 (or omitted) splits at every match.

@param sep   Separator byte sequence. Must not be empty.
@param max?  Maximum number of splits. Defaults to "all".

@return A new List whose elements are zero-copy Bytes slices.

@panic TypeError   When `sep` is not a Bytes or `max` is not an Int.
@panic ValueError  When `sep` is empty.

@see find, count

@example
    b"a,b,c".split(b",")         // [b"a", b"b", b"c"]
    b"a,b,c,d".split(b",", 2)    // [b"a", b"b", b"c,d"]
)DOC", 2, "max", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("sep", false, InstanceType::BYTES),
                   PCHECK_DEF("max", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];
    const auto *sep = (Bytes *) argv[1];
    auto *isolate = O_GET_ISOLATE(self);

    if (sep->length == 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "split separator cannot be empty");

        return {};
    }

    IntegerUnderlying max = -1;
    if (!O_IS_SENTINEL(argv[2])) {
        if (!NumberExtract(argv[2], max))
            return {};
    }

    std::shared_lock<orbiter::sync::AsyncRWLock> s_lock;
    std::shared_lock<orbiter::sync::AsyncRWLock> p_lock;
    LockTwoShared(self->shared, sep->shared, s_lock, p_lock);

    const auto out = support::Split(isolate,
                                    self->shared->buffer + self->start,
                                    self->length,
                                    sep->shared->buffer + sep->start,
                                    sep->length,
                                    [self](orbiter::Isolate *isolate, const unsigned char *buffer, const MSize length) {
                                        const auto start = buffer - (self->shared->buffer + self->start);
                                        return BytesNew(self, start, length);
                                    }, max);

    return HOObject((OObject *) out.get());
}

RUNTIME_METHOD(bytes_starts_with, starts_with,
               R"DOC(
@brief Return true if self starts with the given prefix.

@param prefix  The byte sequence to test.

@return true when self's first `len(prefix)` bytes equal `prefix`,
        false otherwise. An empty prefix returns true.

@panic TypeError  When `prefix` is not a Bytes.

@see ends_with, find

@example
    b"hello.txt".starts_with(b"hello")    // true
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("prefix", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];
    const auto *prefix = (Bytes *) argv[1];

    if (prefix->length > self->length)
        return HOObject((OObject *) BOOL_TO_OBOOL(false));

    if (prefix->length == 0)
        return HOObject((OObject *) BOOL_TO_OBOOL(true));

    std::shared_lock<orbiter::sync::AsyncRWLock> s_lock;
    std::shared_lock<orbiter::sync::AsyncRWLock> p_lock;
    LockTwoShared(self->shared, prefix->shared, s_lock, p_lock);

    const auto eq = memcmp(self->shared->buffer + self->start,
                           prefix->shared->buffer + prefix->start,
                           prefix->length) == 0;

    return HOObject((OObject *) BOOL_TO_OBOOL(eq));
}

RUNTIME_METHOD(bytes_strip, strip,
               R"DOC(
@brief Remove leading and trailing bytes in `chars` from self in place.

The view's `start` and `length` are adjusted to exclude the stripped
prefix and suffix — no allocation, no buffer copy. Co-owners of the
SharedBuffer are unaffected.

When `chars` is omitted, the default whitespace set is used (see `lstrip`).

@param chars?  Set of bytes to strip. Defaults to ASCII whitespace.

@return Self.

@panic TypeError   When `chars` is not a Bytes.
@panic ValueError  When the Bytes is frozen.

@see lstrip, rstrip

@example
    let b = Bytes(b"  hello  ")
    b.strip()
    b               // b"hello"
)DOC", 1, "chars", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::BYTES),
                   PCHECK_DEF("chars", true, InstanceType::BYTES));
    PCHECK_CHECK(params);

    auto *self = (Bytes *) argv[0];
    const auto *chars_b = O_IS_SENTINEL(argv[1]) ? nullptr : (const Bytes *) argv[1];

    std::shared_lock<orbiter::sync::AsyncRWLock> s_lock;
    std::shared_lock<orbiter::sync::AsyncRWLock> c_lock;
    if (chars_b != nullptr)
        LockTwoShared(self->shared, chars_b->shared, s_lock, c_lock);
    else
        s_lock = std::shared_lock(self->shared->rwlock);

    if (!CheckMutable(self))
        return {};

    const unsigned char *cset = chars_b != nullptr ? chars_b->shared->buffer + chars_b->start : kDefaultStripChars;
    const MSize cset_len = chars_b != nullptr ? chars_b->length : kDefaultStripCharsLen;

    MSize i = 0;
    while (i < self->length && ByteInSet(self->shared->buffer[self->start + i], cset, cset_len))
        i++;

    MSize end = self->length;
    while (end > i && ByteInSet(self->shared->buffer[self->start + end - 1], cset, cset_len))
        end--;

    self->start += i;
    self->length = end - i;

    return HOObject(argv[0]);
}

RUNTIME_METHOD(bytes_upper, upper,
               R"DOC(
@brief Uppercase every ASCII lowercase byte in place.

Bytes outside `[0x61, 0x7A]` ('a'..'z') are left untouched — non-ASCII
content is preserved bit-for-bit. To obtain an uppercased copy without
modifying self, call `copy()` first.

@return Self.

@panic ValueError  When the Bytes is frozen.

@see lower, copy

@example
    let b = Bytes(b"Hello")
    b.upper()
    b               // b"HELLO"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::BYTES));
    PCHECK_CHECK(params);

    const auto *self = (Bytes *) argv[0];

    std::unique_lock _(self->shared->rwlock);

    if (!CheckMutable(self))
        return {};

    auto *buf = self->shared->buffer + self->start;
    for (MSize i = 0; i < self->length; i++) {
        const auto b = buf[i];

        if (b >= 'a' && b <= 'z')
            buf[i] = (unsigned char) (b - 0x20);
    }

    return HOObject(argv[0]);
}

constexpr FunctionDef bytes_methods[] = {
    bytes_create,

    bytes_append,
    bytes_clear,
    bytes_compact,
    bytes_contains,
    bytes_count,
    bytes_ends_with,
    bytes_extend,
    bytes_find,
    bytes_freeze,
    bytes_hex,
    bytes_is_ascii,
    bytes_is_frozen,
    bytes_length,
    bytes_lower,
    bytes_lstrip,
    bytes_replace,
    bytes_rfind,
    bytes_rstrip,
    bytes_split,
    bytes_starts_with,
    bytes_strip,
    bytes_upper,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::BytesAppend(Bytes *bytes, const Bytes *other) noexcept {
    std::unique_lock a_lock(bytes->shared->rwlock, std::defer_lock);
    std::shared_lock b_lock(other->shared->rwlock, std::defer_lock);

    auto *isolate = O_GET_ISOLATE(bytes);

    LockTwoSelfUnique(bytes->shared, other->shared, a_lock, b_lock);

    if (!CheckMutable(bytes))
        return false;

    const auto *other_buffer = other->shared->buffer;
    if (bytes->shared == other->shared) {
        // If destination and source are the same, make an initial call to reallocate the buffer (if necessary)
        // and update the buffer pointer to avoid dangling pointer scenarios
        if (!support::SharedBufferAppendLocked(isolate,
                                               bytes->shared,
                                               nullptr,
                                               bytes->start + bytes->length,
                                               other->length))
            return false;

        other_buffer = other->shared->buffer;
    }

    if (other->length == 0)
        return true;

    if (!support::SharedBufferAppendLocked(isolate,
                                           bytes->shared,
                                           other_buffer + other->start,
                                           bytes->start + bytes->length,
                                           other->length))
        return false;

    bytes->length += other->length;

    return true;
}

bool orbiter::datatype::BytesAppendData(Bytes *bytes, const unsigned char *buffer, const MSize length) noexcept {
    std::unique_lock s_lock(bytes->shared->rwlock);

    if (!CheckMutable(bytes))
        return false;

    if (length == 0)
        return true;

    if (!support::SharedBufferAppendLocked(O_GET_ISOLATE(bytes), bytes->shared, buffer, bytes->start + bytes->length,
                                           length))
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

    if (!TIPropertyAdd(self, bytes_methods, PropertyFlag::IS_PUBLIC))
        return false;

    const auto ctor = FunctionFromDef(self, bytes_create);
    if (!ctor)
        return false;

    self->ctor = (OObject *) ctor.get();

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

HBytes orbiter::datatype::BytesNew(Isolate *isolate, OObject *object) noexcept {
    if (O_IS_TYPE(object, InstanceType::BYTES)) {
        const auto *other = (Bytes *) object;

        std::shared_lock _(other->shared->rwlock);

        const auto bytes = BytesNew(isolate, other->shared->buffer + other->start, other->length, false);
        if (!bytes)
            return {};

        return (HBytes) bytes.get();
    }

    if (O_IS_TYPE(object, InstanceType::STRING)) {
        const auto *other = (ORString *) object;

        const auto bytes = BytesNew(isolate, other->buffer, other->length, false);
        if (!bytes)
            return {};

        return (HBytes) bytes.get();
    }

    if (O_IS_TYPE(object, InstanceType::LIST) || O_IS_TYPE(object, InstanceType::TUPLE)) {
        auto *iterator = O_GET_TYPE_OPS(object).get_iter(object);
        if (iterator == nullptr)
            return {};

        OObject *item;

        const auto length = O_IS_TYPE(object, InstanceType::LIST)
                                ? ((List *) object)->length
                                : ((Tuple *) object)->length;
        const auto bytes = BytesNew(isolate, length, false);
        if (!bytes)
            return {};

        auto *next_fn = O_GET_TYPE_OPS(iterator).iter_next;

        while (true) {
            const auto call_result = next_fn(iterator, &item);
            if (call_result == CallResult::EXHAUST)
                break;

            if (call_result == CallResult::ERROR)
                return {};

            unsigned char byte;
            if (!ExtractByte(isolate, item, &byte))
                return {};

            if (!BytesAppendData(bytes.get(), &byte, 1))
                return {};
        }

        return (HBytes) bytes.get();
    }

    ErrorSetWithObjType(isolate,
                        TypeError::Details[TypeError::Reason::ID],
                        TypeError::Details[TypeError::Reason::MISMATCH],
                        "Bytes, List, String or Tuple",
                        object);

    return {};
}

HOType orbiter::datatype::BytesTypeInit(Isolate *isolate) {
    return MakeType(isolate, "Bytes", InstanceType::BYTES, sizeof(Bytes) - sizeof(OObject), 24, 0);
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
