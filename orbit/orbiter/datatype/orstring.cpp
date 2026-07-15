// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <cctype>
#include <cstdarg>
#include <shared_mutex>

#include <orbit/util/hash.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/hashmap.h>
#include <orbit/orbiter/datatype/iterator.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/stringbuilder.h>
#include <orbit/orbiter/datatype/stringformatter.h>

#include <orbit/orbiter/datatype/support/byteops.h>
#include <orbit/orbiter/datatype/support/common.h>

#include <orbit/orbiter/datatype/orstring.h>

using namespace orbiter::datatype;

#define STR_BUF(str) ((str)->buffer)
#define STR_LEN(str) ((str)->length)

using StringEntry = HEntry<ORString *, ORString *>;
using StringMap = HashMap<
    ORString *,
    ORString *,
    static_cast<bool (*)(const ORString *, const ORString *)>(ORStringEqual),
    ORStringHash
>;

class GST {
public:
    std::shared_mutex lock;

    StringMap map;

    ORString *empty = nullptr;

    explicit GST(orbiter::Isolate *isolate) : map(isolate) {
    }
};

bool StrDtor(const ORString *self) {
    const orbiter::memory::IsolateAllocator allocator(O_GET_ISOLATE(self));

    allocator.free(self->buffer);

    return true;
}

bool StrGSTDtor(TypeInfo *self) {
    auto *gst = (GST *) self->aux.data;

    gst->map.Finalize([](const StringEntry *entry) {
        O_FAST_DECREF(entry->key);
        O_FAST_DECREF(entry->value);
    });

    const orbiter::memory::IsolateAllocator allocator(self->isolate);
    allocator.FreeObject(gst);

    self->aux.data = nullptr;

    return true;
}

bool StrEqual(const ORString *left, const ORString *right) {
    if (left == right)
        return true;

    if (!O_IS_TYPE(right, InstanceType::STRING))
        return false;

    if (STR_LEN(left) != STR_LEN(right))
        return false;

    return ORStringEqual(left, right);
}

/// ops.equal adapter: string equality cannot fail, but the slot's signature
/// is fallible (user hooks elsewhere can panic mid equality chains).
static bool StrOpEqual(const OObject *left, const OObject *right, bool &out) {
    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::STRING)) {
        out = false;

        return true;
    }

    out = StrEqual((const ORString *) left, (const ORString *) right);

    return true;
}

bool StrRawEqual(const ORString *left, const unsigned char *right, const MSize length) {
    return ORStringCompare(left, (const char *) right, length) == 0;
}

bool StrToNative(ORString *self, void *out, const NativeType type) {
    if (type == NativeType::PTR) {
        *((PtrSize *) out) = (PtrSize) STR_BUF(self);

        return true;
    }

    return false;
}

bool StringInitKind(ORString *string) {
    auto kind = StringKind::ASCII;

    MSize index = 0;
    MSize cp_length = 0;

    string->cp_length = 0;

    while (index < string->length) {
        if (!CheckUnicodeCharSequence(&kind, &cp_length, nullptr, 0, string->buffer[index], index)) {
            const auto reason = (index == cp_length)
                                    ? UnicodeError::Reason::INVALID_START_BYTE
                                    : UnicodeError::Reason::INVALID_CONTINUATION_BYTE;

            ErrorSet(O_GET_ISOLATE(string),
                     UnicodeError::Details[UnicodeError::Reason::ID],
                     nullptr,
                     UnicodeError::Details[reason],
                     string->buffer[index]);

            return false;
        }

        if (kind > string->kind)
            string->kind = kind;

        if (++index == cp_length)
            string->cp_length++;
    }

    return true;
}

ORString *MkStringContainer(orbiter::Isolate *isolate, const MSize len, const bool mkbuf) {
    const auto str = MakeObject<ORString>(isolate, InstanceType::STRING);

    if (str != nullptr) {
        str->buffer = nullptr;

        if (mkbuf) {
            // +1 is '\0'
            orbiter::memory::IsolateAllocator allocator(isolate);
            str->buffer = allocator.alloc<unsigned char>(len + 1);
            if (str->buffer == nullptr) {
                isolate->gc->RawFree((OObject *) str, false);

                return nullptr;
            }

            // Set terminator
            STR_BUF(str)[(len + 1) - 1] = 0x00;
        }

        str->kind = StringKind::ASCII;
        str->intern = false;
        STR_LEN(str) = len;
        str->cp_length = 0;
        str->hash = 0;
    }

    return str;
}

MSize StrHash(const unsigned char *buffer, const MSize length) {
    auto hash = fnv1_hash(buffer, length);
    if (hash == 0 || hash == HASH_ERROR)
        hash = 1;

    return hash;
}

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

static bool StrCompare(const OObject *left, const OObject *right, int &result) {
    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::STRING))
        return false;

    result = ORStringCompare((const ORString *) left, (const ORString *) right);

    return true;
}

/// Membership test: `"ab" in "xabc"` — requires a String operand.
static bool StrContains(const OObject *container, const OObject *value, bool &result) {
    if (!O_IS_OBJECT(value) || !O_IS_TYPE(value, InstanceType::STRING)) {
        ErrorSetWithObjType(O_GET_ISOLATE(container),
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::MISMATCH],
                            O_GET_TYPE(container)->name,
                            value);

        return false;
    }

    result = ORStringContains((const ORString *) container, (const ORString *) value);

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — ARITHMETIC
// *********************************************************************************************************************

/// String concatenation: "a" + "b" → "ab".
static bool StrAdd(const OObject *left, const OObject *right, OObject *&result) {
    auto *isolate = O_GET_ISOLATE(left);

    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::STRING))
        return false;

    const auto *l = (const ORString *) left;
    const auto *r = (const ORString *) right;
    const MSize total = STR_LEN(l) + STR_LEN(r);

    auto *new_string = MkStringContainer(isolate, total, true);
    if (new_string == nullptr)
        return false;

    memcpy(STR_BUF(new_string), STR_BUF(l), STR_LEN(l));
    memcpy(STR_BUF(new_string) + STR_LEN(l), STR_BUF(r), STR_LEN(r));
    STR_BUF(new_string)[total] = '\0';

    new_string->kind = l->kind > r->kind ? l->kind : r->kind;
    new_string->cp_length = l->cp_length + r->cp_length;

    isolate->gc->Track((OObject *) new_string, false);

    result = (OObject *) new_string;

    return true;
}

/// String repetition: "ab" * 3 → "ababab".
static bool StrMul(const OObject *left, const OObject *right, OObject *&result) {
    auto *isolate = O_GET_ISOLATE(left);

    IntegerUnderlying n;
    if (!NumberExtract(right, n))
        return false;

    if (n <= 0) {
        result = (OObject *) ORStringNew(isolate, "", 0).get();

        return true;
    }

    auto *self = (const ORString *) left;
    const auto length = STR_LEN(self);

    if (length > 0 && (UIntegerUnderlying) n > ((MSize) -1 / length)) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "string repetition count is too large");

        return false;
    }

    const auto total = length * (MSize) n;

    auto *new_string = MkStringContainer(isolate, total, true);
    if (new_string == nullptr)
        return false;

    for (MSize i = 0; i < (MSize) n; i++)
        memcpy(STR_BUF(new_string) + (i * length), STR_BUF(self), length);

    STR_BUF(new_string)[total] = '\0';

    new_string->kind = self->kind;
    new_string->cp_length = self->cp_length * (MSize) n;

    isolate->gc->Track((OObject *) new_string, false);

    result = (OObject *) new_string;

    return true;
}

/// printf-style formatting: "hello %s" % name.
static bool StrMod(const OObject *left, const OObject *right, OObject *&result) {
    auto *self = (const ORString *) left;

    StringFormatter sf(O_GET_ISOLATE(self), (const char *) STR_BUF(self), STR_LEN(self), (OObject *) right, false);
    const auto s = sf.FormatToString();
    if (sf.HasError() || !s)
        return false;

    result = (OObject *) s.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — ITERATION
// *********************************************************************************************************************

/// Decode the byte length of a single UTF-8 codepoint from its lead byte.
/// Returns 1..4 on success, 0 if the byte is not a valid UTF-8 lead.
static MSize StrUtf8LeadByteCount(const unsigned char c) {
    if ((c & 0x80) == 0x00) return 1; // 0xxxxxxx — ASCII
    if ((c & 0xE0) == 0xC0) return 2; // 110xxxxx
    if ((c & 0xF0) == 0xE0) return 3; // 1110xxxx
    if ((c & 0xF8) == 0xF0) return 4; // 11110xxx

    return 0;
}

/// Strip-set membership for the codepoint-aware strip / lstrip / rstrip
/// family. @p cp points at the @p cp_len bytes of a single UTF-8
/// codepoint of the subject string.
///
/// When @p chars is null the default ASCII whitespace set is used —
/// every whitespace byte is single-byte, so a multi-byte codepoint can
/// never be whitespace. Otherwise @p chars is treated as a *set of
/// codepoints*: we walk its codepoints and match @p cp byte-for-byte
/// (same length + identical bytes). Subject and chars are validated
/// UTF-8 (StringInitKind), so a malformed lead defensively advances one
/// byte without matching.
static bool CodepointInChars(const unsigned char *cp, const MSize cp_len, const ORString *chars) {
    if (chars == nullptr)
        return cp_len == 1 && support::IsAsciiWhitespace(cp[0]);

    MSize i = 0;
    while (i < STR_LEN(chars)) {
        const auto clen = StrUtf8LeadByteCount(STR_BUF(chars)[i]);
        if (clen == 0 || i + clen > STR_LEN(chars)) {
            i += 1; // defensive: malformed, skip a byte
            continue;
        }

        if (clen == cp_len && memcmp(STR_BUF(chars) + i, cp, cp_len) == 0)
            return true;

        i += clen;
    }

    return false;
}

/// Convert a byte offset within a well-formed UTF-8 string into the index of
/// the codepoint that begins at @p byte. ASCII strings map 1:1, so the byte
/// offset is already the codepoint index; otherwise we count every codepoint
/// lead byte (any byte that is not a 10xxxxxx continuation) in [0, byte).
///
/// This is how byte-oriented search primitives (`find` / `rfind`) report
/// their result in the same codepoint units used by `length` and `substring`.
static MSize StrByteToCodepoint(const ORString *self, const MSize byte) {
    if (self->kind == StringKind::ASCII)
        return byte;

    MSize cp = 0;
    for (MSize i = 0; i < byte; i++)
        if ((STR_BUF(self)[i] & 0xC0) != 0x80)
            cp++;

    return cp;
}

/// Byte offset of the start of the last UTF-8 codepoint that ends at
/// @p end (exclusive). Walks back over continuation bytes (10xxxxxx).
static MSize StrLastCodepointStart(const ORString *s, const MSize end) {
    MSize cp_start = end - 1;
    while (cp_start > 0 && (STR_BUF(s)[cp_start] & 0xC0) == 0x80)
        cp_start--;

    return cp_start;
}

/// Walk the string one codepoint at a time, yielding each codepoint as a fresh single-codepoint ORString.
///
/// Allocation policy:
///   - 1-byte (ASCII) codepoints go through `ORStringIntern`, which reuses
///     the per-isolate pool — repeated occurrences of the same character
///     all return the same ORString instance, no per-step allocation.
///   - Multi-byte codepoints go through `ORStringNew`, which copies the
///     2..4 bytes into a fresh buffer.
static CallResult StrIterStep(Iterator *self, OObject **out) {
    const auto *src = (const ORString *) self->source;
    auto *isolate = O_GET_ISOLATE(src);

    if (self->state.str.byte >= STR_LEN(src))
        return CallResult::EXHAUST;

    const auto offset = self->state.str.byte;
    const unsigned char *p = STR_BUF(src) + offset;

    const auto cp_bytes = StrUtf8LeadByteCount(*p);
    if (cp_bytes == 0 || offset + cp_bytes > STR_LEN(src)) {
        // Defensive: a well-formed ORString never reaches this branch.
        ErrorSet(isolate,
                 UnicodeError::Details[UnicodeError::Reason::ID],
                 nullptr,
                 UnicodeError::Details[UnicodeError::Reason::INVALID_START_BYTE],
                 (unsigned int) *p);

        return CallResult::ERROR;
    }

    const HORString cp_str = (cp_bytes == 1) ? ORStringIntern(isolate, p, 1) : ORStringNew(isolate, p, cp_bytes);
    if (!cp_str)
        return CallResult::ERROR;

    self->state.str.byte += cp_bytes;
    self->state.str.cp += 1;

    *out = (OObject *) cp_str.get();

    return CallResult::DONE;
}

/// Build a fresh iterator over @p self. ORString is immutable, so we leave
/// `snapshot_length` at 0 — the step never checks it.
static OObject *StrGetIter(OObject *self) {
    const auto iter = IteratorNew(O_GET_ISOLATE(self), self, StrIterStep);
    if (!iter)
        return nullptr;

    iter->state.str.byte = 0;
    iter->state.str.cp = 0;

    return (OObject *) iter.get();
}

/// Walk the string backwards one codepoint at a time. To find the previous
/// codepoint we rewind byte by byte over UTF-8 continuation bytes
/// (10xxxxxx), then identify the lead byte and emit the codepoint as a
/// fresh single-codepoint ORString.
///
/// Worst-case rewind cost is 3 bytes (UTF-8 caps at 4 bytes/codepoint).
static CallResult StrIterStepReverse(Iterator *self, OObject **out) {
    const auto *src = (const ORString *) self->source;
    auto *isolate = O_GET_ISOLATE(src);

    if (self->state.str.byte == 0)
        return CallResult::EXHAUST;

    // Rewind to the previous codepoint's lead byte. Continuation bytes
    // match the pattern 10xxxxxx → (b & 0xC0) == 0x80.
    auto offset = self->state.str.byte;
    do {
        offset -= 1;
    } while (offset > 0 && (STR_BUF(src)[offset] & 0xC0) == 0x80);

    const unsigned char *p = STR_BUF(src) + offset;
    const auto cp_bytes = StrUtf8LeadByteCount(*p);

    if (cp_bytes == 0 || offset + cp_bytes > self->state.str.byte) {
        // Defensive: a well-formed ORString never reaches this branch.
        ErrorSet(isolate,
                 UnicodeError::Details[UnicodeError::Reason::ID],
                 nullptr,
                 UnicodeError::Details[UnicodeError::Reason::INVALID_START_BYTE],
                 (unsigned int) *p);

        return CallResult::ERROR;
    }

    const HORString cp_str = (cp_bytes == 1) ? ORStringIntern(isolate, p, 1) : ORStringNew(isolate, p, cp_bytes);
    if (!cp_str)
        return CallResult::ERROR;

    self->state.str.byte = offset;
    self->state.str.cp -= 1;

    *out = (OObject *) cp_str.get();

    return CallResult::DONE;
}

/// Build a fresh reverse iterator over @p self. Cursor starts past-the-end
/// in both byte and codepoint coordinates; first step rewinds before reading.
static OObject *StrGetReverseIter(OObject *self) {
    const auto *src = (const ORString *) self;

    const auto iter = IteratorNew(O_GET_ISOLATE(self), self, StrIterStepReverse);
    if (!iter)
        return nullptr;

    iter->state.str.byte = STR_LEN(src);
    iter->state.str.cp = src->cp_length;
    iter->reverse = true;

    return (OObject *) iter.get();
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A non-empty string is truthy.
static bool StrToBool(const OObject *self) {
    return STR_LEN((const ORString *) self) != 0;
}

/// Return the string itself as its string representation.
static OObject *StrToString(orbiter::Isolate *, const OObject *self) {
    return (OObject *) self;
}

/// Return a double-quoted escaped representation, e.g. "hello\nworld".
static OObject *StrToRepr(orbiter::Isolate *isolate, const ORString *self) {
    constexpr unsigned char quote = '"';

    StringBuilder builder(isolate);

    if (!builder.Write(&quote, 1, STR_LEN(self) + 2))
        return nullptr;

    if (!builder.WriteEscaped(STR_BUF(self), STR_LEN(self), 0))
        return nullptr;

    if (!builder.Write(&quote, 1, 0))
        return nullptr;

    MSize len;
    MSize cp_len;
    StringKind kind;

    auto *buf = builder.BuildString(nullptr, &len, &cp_len, &kind);
    if (buf == nullptr)
        return nullptr;

    const auto s = ORStringNew(isolate, buf, len, cp_len, kind);
    if (!s)
        return nullptr;

    builder.Release();

    return (OObject *) s.get();
}

// *********************************************************************************************************************
// TYPE OPS — RUNTIME
// *********************************************************************************************************************

static MSize StrHashOp(const OObject *self) {
    return ORStringHash((ORString *) self);
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_METHOD(string_at, at,
               R"DOC(
@brief Return the codepoint at the given index as a String.

A simpler single-codepoint form of `substring`: `s.at(i)` is equivalent to
`s.substring(i, i + 1)`. The index is measured in Unicode codepoints, so this
is O(1) for ASCII strings and O(n) for strings that contain multi-byte
codepoints.

@param index  Codepoint index (0-based).

@return A new String holding the single codepoint at `index`.

@panic TypeError   When `index` is not a Number.
@panic IndexError  When `index` is outside 0 <= index < length().

@see substring, length

@example
    "hello".at(1)     // "e"
    "héllo".at(1)     // "é"
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("index", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying index;
    if (!NumberExtract(argv[1], index))
        return {};

    if (index < 0 || (MSize) index >= self->cp_length) {
        ErrorSet(isolate,
                 IndexError::Details[IndexError::Reason::ID],
                 nullptr,
                 IndexError::Details[IndexError::Reason::OUT_OF_RANGE],
                 O_GET_TYPE(self)->name,
                 index,
                 self->cp_length);

        return {};
    }

    // Locate the codepoint's byte offset: ASCII maps 1:1, otherwise walk.
    auto byte = (MSize) 0;
    if (self->kind == StringKind::ASCII)
        byte = (MSize) index;
    else {
        for (MSize cp = 0; cp < (MSize) index; cp++)
            byte += StrUtf8LeadByteCount(STR_BUF(self)[byte]);
    }

    const unsigned char *p = STR_BUF(self) + byte;
    const auto cp_bytes = StrUtf8LeadByteCount(*p);

    // Single-byte codepoints are interned (shared); multi-byte are copied.
    auto s = (cp_bytes == 1) ? ORStringIntern(isolate, p, 1) : ORStringNew(isolate, p, cp_bytes);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(string_byte_length, byte_length,
               R"DOC(
@brief Return the length of the string in bytes.

Unlike `length`, which counts Unicode codepoints, this returns the size of
the underlying UTF-8 buffer. The two match for ASCII strings and diverge as
soon as a multi-byte codepoint appears — useful when handing the string to
byte-oriented APIs (FFI, I/O) that need the real buffer size.

@return A non-negative Int.

@see length, is_ascii

@example
    "hello".byte_length()    // 5  (same as length())
    "héllo".byte_length()    // 6  (é takes 2 bytes; length() is 5)
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];

    auto result = IntNew(O_GET_ISOLATE(self), (IntegerUnderlying) STR_LEN(self));
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(string_byte_substring, byte_substring,
               R"DOC(
@brief Return the substring spanning a half-open range of byte offsets.

The byte-offset counterpart of `substring`: `start` and `end` are positions in
the underlying UTF-8 buffer, with `end` exclusive. Unlike `substring` this never
scans the string, so it is O(1) regardless of content — but the offsets must fall
on codepoint boundaries: a range that would split a multi-byte codepoint is
rejected rather than producing invalid UTF-8.

@param start  Byte offset of the first byte to include (0-based).
@param end    Byte offset one past the last byte to include.

@return A new String with the bytes in [start, end); empty when start == end.

@panic TypeError   When `start` or `end` is not a Number.
@panic ValueError  When the bounds fall outside 0 <= start <= end <= byte_length(),
                   or either bound splits a multi-byte codepoint.

@see substring, byte_length, find

@example
    "hello".byte_substring(1, 4)    // "ell"
    "héllo".byte_substring(0, 3)    // "hé"  (é spans bytes 1..2)
    "héllo".byte_substring(0, 2)    // ValueError: splits 'é'
)DOC", 3, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("start", false, InstanceType::NUMBER),
                   PCHECK_DEF("end", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying start;
    IntegerUnderlying end;
    if (!NumberExtract(argv[1], start) || !NumberExtract(argv[2], end))
        return {};

    if (start < 0 || end < start || (MSize) end > STR_LEN(self)) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "byte_substring bounds out of range");

        return {};
    }

    // A bound is valid only on a codepoint boundary: either the end of the
    // buffer or a non-continuation byte (UTF-8 continuations are 10xxxxxx).
    // ASCII strings have no continuation bytes, so the check is skipped.
    if (self->kind != StringKind::ASCII) {
        const auto splits = [self](const MSize pos) {
            return pos < STR_LEN(self) && (STR_BUF(self)[pos] & 0xC0) == 0x80;
        };

        if (splits(start) || splits(end)) {
            ErrorSet(isolate,
                     ValueError::Details[ValueError::Reason::ID],
                     nullptr,
                     "byte_substring bounds split a multi-byte codepoint");

            return {};
        }
    }

    auto s = ORStringNew(isolate, STR_BUF(self) + start, end - start);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(string_next_codepoint, next_codepoint,
               R"DOC(
@brief Return the byte offset of the codepoint boundary after `offset`.

Given a byte offset into the UTF-8 buffer, advance to the start of the next
codepoint: `offset` plus the byte length of the codepoint that begins there.
If `offset` falls *inside* a multi-byte codepoint, it advances to the next
lead byte (never splitting further). This is the primitive for stepping a byte
offset forward without landing mid-codepoint — e.g. advancing past a
zero-width regex match on a UTF-8 subject.

@param offset  A byte offset, 0 <= offset <= byte_length().

@return The byte offset of the next codepoint boundary. Equals byte_length()
        once the end is reached (advancing at the end stays at the end).

@panic TypeError   When `offset` is not a Number.
@panic ValueError  When `offset` is outside 0 <= offset <= byte_length().

@see byte_length, byte_substring, at

@example
    "héllo".next_codepoint(0)    // 1  (h is 1 byte)
    "héllo".next_codepoint(1)    // 3  (é spans bytes 1..2)
    "héllo".next_codepoint(2)    // 3  (offset was mid-é; snap to next boundary)
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("offset", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying offset;
    if (!NumberExtract(argv[1], offset))
        return {};

    if (offset < 0 || (MSize) offset > STR_LEN(self)) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "next_codepoint offset out of range");

        return {};
    }

    auto pos = (MSize) offset;

    if (pos < STR_LEN(self)) {
        // If we're mid-codepoint (a 10xxxxxx continuation byte), the lead-byte
        // count is 0; step over continuation bytes to the next boundary.
        // Otherwise advance by the codepoint's byte length (clamped to the end).
        const auto width = StrUtf8LeadByteCount(STR_BUF(self)[pos]);

        if (width == 0) {
            do
                pos++;
            while (pos < STR_LEN(self) && (STR_BUF(self)[pos] & 0xC0) == 0x80);
        } else {
            pos += width;

            if (pos > STR_LEN(self))
                pos = STR_LEN(self);
        }
    }

    auto result = IntNew(isolate, (IntegerUnderlying) pos);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(string_contains, contains,
               R"DOC(
@brief Return true if the string contains the given substring.

@param sub  The substring to search for.

@return true if found, false otherwise.

@panic TypeError  When `sub` is not a String.

@see find, count

@example
    "hello".contains("ell")    // true
    "hello".contains("xyz")    // false
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("sub", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    return HOObject((OObject *) BOOL_TO_OBOOL(ORStringContains((ORString *) argv[0], (ORString *) argv[1])));
}

RUNTIME_METHOD(string_count, count,
               R"DOC(
@brief Return the number of non-overlapping occurrences of sub in the string.

Returns 0 when sub is empty.

@param sub  The substring to count.

@return A non-negative Int.

@panic TypeError  When `sub` is not a String.

@see contains, find

@example
    "banana".count("an")    // 2
    "hello".count("l")      // 2
    "hello".count("x")      // 0
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("sub", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *sub = (ORString *) argv[1];

    IntegerUnderlying n = 0;

    if (STR_LEN(sub) > 0) {
        MSize pos = 0;

        while (pos + STR_LEN(sub) <= STR_LEN(self)) {
            const auto idx = support::Search(STR_BUF(self) + pos, STR_LEN(self) - pos,STR_BUF(sub), STR_LEN(sub));
            if (idx < 0)
                break;

            pos += (MSize) idx + STR_LEN(sub);

            n += 1;
        }
    }

    auto result = IntNew(O_GET_ISOLATE(self), n);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(string_ends_with, ends_with,
               R"DOC(
@brief Return true if the string ends with the given suffix.

@param suffix  The suffix to check.

@return true or false.

@panic TypeError  When `suffix` is not a String.

@see starts_with

@example
    "hello".ends_with("llo")    // true
    "hello".ends_with("hel")    // false
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("suffix", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *suffix = (ORString *) argv[1];

    const auto ok = ORStringEndsWith(self, suffix);

    return HOObject((OObject *) BOOL_TO_OBOOL(ok));
}

RUNTIME_METHOD(string_find, find,
               R"DOC(
@brief Return the codepoint index of the first occurrence of sub, or -1.

The index is measured in Unicode codepoints — consistent with `length` and
`substring` — so `s.substring(s.find(x), s.length())` yields the tail of the
string starting at the first match, for ASCII and multi-byte strings alike.

@param sub  The substring to search for.

@return An Int codepoint index, or -1 if not found.

@panic TypeError  When `sub` is not a String.

@see rfind, contains, substring

@example
    "hello".find("ll")     // 2
    "héllo".find("llo")    // 2  (codepoints, not bytes)
    "hello".find("x")      // -1
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("sub", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *sub = (ORString *) argv[1];

    const auto idx = support::Search(STR_BUF(self), STR_LEN(self), STR_BUF(sub), STR_LEN(sub));

    const IntegerUnderlying cp_idx = idx < 0
                                         ? -1
                                         : (IntegerUnderlying) StrByteToCodepoint(self, (MSize) idx);

    auto result = IntNew(O_GET_ISOLATE(self), cp_idx);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(string_is_ascii, is_ascii,
               R"DOC(
@brief Return true if all bytes in the string are ASCII (< 128).

@return true or false.

@see bytes

@example
    "hello".is_ascii()    // true
    "héllo".is_ascii()    // false
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    return HOObject((OObject *) BOOL_TO_OBOOL(((ORString *) argv[0])->kind == StringKind::ASCII));
}

RUNTIME_METHOD(string_length, length,
               R"DOC(
@brief Return the number of Unicode codepoints in the string.

For ASCII strings this equals the byte length.

@return A non-negative Int.

@see bytes

@example
    "hello".length()    // 5
    "".length()         // 0
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];

    auto result = IntNew(O_GET_ISOLATE(self), (IntegerUnderlying) self->cp_length);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(string_lower, lower,
               R"DOC(
@brief Return a copy of the string with ASCII letters converted to lowercase.

Non-ASCII bytes are passed through unchanged.

@return A new String.

@see upper

@example
    "Hello World".lower()    // "hello world"
    "ABC123".lower()         // "abc123"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    auto *result = MkStringContainer(isolate, STR_LEN(self), true);
    if (result == nullptr)
        return {};

    for (MSize i = 0; i < STR_LEN(self); i++)
        STR_BUF(result)[i] = STR_BUF(self)[i] < 0x80u
                                 ? (unsigned char) std::tolower(STR_BUF(self)[i])
                                 : STR_BUF(self)[i];

    STR_BUF(result)[STR_LEN(self)] = '\0';

    result->kind = self->kind;
    result->cp_length = self->cp_length;

    isolate->gc->Track((OObject *) result, false);

    return HOObject((OObject *) result);
}

RUNTIME_METHOD(string_lstrip, lstrip,
               R"DOC(
@brief Return a copy with leading characters in `chars` removed.

When `chars` is omitted the default ASCII whitespace set is used
(` `, `\t`, `\n`, `\v`, `\f`, `\r`). When provided, `chars` is treated
as a set of codepoints: leading codepoints of self that appear in
`chars` are removed (full Unicode-aware, multi-byte safe).

@param chars?  Set of codepoints to strip. Defaults to ASCII whitespace.

@return A new String, or self if no stripping is needed.

@panic TypeError  When `chars` is not a String.

@see rstrip, strip

@example
    "  hello  ".lstrip()       // "hello  "
    "xxhello".lstrip("x")      // "hello"
)DOC", 1, "chars", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("chars", true, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *chars = O_IS_SENTINEL(argv[1]) ? nullptr : (const ORString *) argv[1];
    auto *isolate = O_GET_ISOLATE(_func);

    MSize start = 0;
    while (start < STR_LEN(self)) {
        const auto clen = StrUtf8LeadByteCount(STR_BUF(self)[start]);
        if (clen == 0 || start + clen > STR_LEN(self))
            break; // defensive: malformed lead

        if (!CodepointInChars(STR_BUF(self) + start, clen, chars))
            break;

        start += clen;
    }

    if (start == 0)
        return HOObject(argv[0]);

    if (start == STR_LEN(self))
        return HOObject(ORStringNew(isolate, "", 0));

    auto s = ORStringNew(isolate, STR_BUF(self) + start, STR_LEN(self) - start);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(string_replace, replace,
               R"DOC(
@brief Return a copy with all non-overlapping occurrences of old replaced by new.

Returns self unchanged when old is empty.

@param old   Substring to replace.
@param new   Replacement string.
@param max?  Maximum number of replacements. -1 (or omitted) replaces
             every non-overlapping occurrence.

@return A new String.

@panic TypeError  When `old`, `new` or `max` has the wrong type.

@see find, split

@example
    "hello".replace("l", "r")          // "herro"
    "ababab".replace("a", "X", 2)      // "XbXbab"
)DOC", 3, "max", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("old", false, InstanceType::STRING),
                   PCHECK_DEF("new", false, InstanceType::STRING),
                   PCHECK_DEF("max", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *old_str = (ORString *) argv[1];
    const auto *new_str = (ORString *) argv[2];
    auto *isolate = O_GET_ISOLATE(_func);

    if (STR_LEN(old_str) == 0)
        return HOObject(argv[0]);

    MSSize max = -1;
    if (!O_IS_SENTINEL(argv[3])) {
        if (!NumberExtract(argv[3], (IntegerUnderlying &) max))
            return {};
    }

    StringBuilder builder(isolate);
    MSize pos = 0;
    MSSize done = 0;

    while (pos < STR_LEN(self)) {
        // Replacement budget exhausted: flush the untouched remainder.
        if (max >= 0 && done >= max) {
            if (!builder.Write(STR_BUF(self) + pos, STR_LEN(self) - pos, 0))
                return {};

            break;
        }

        const auto idx = support::Search(STR_BUF(self) + pos, STR_LEN(self) - pos,
                                         STR_BUF(old_str), STR_LEN(old_str));
        if (idx < 0) {
            if (!builder.Write(STR_BUF(self) + pos, STR_LEN(self) - pos, 0))
                return {};

            break;
        }

        if ((MSize) idx > 0 && !builder.Write(STR_BUF(self) + pos, (MSize) idx, 0))
            return {};

        if (STR_LEN(new_str) > 0 && !builder.Write(STR_BUF(new_str), STR_LEN(new_str), 0))
            return {};

        pos += (MSize) idx + STR_LEN(old_str);
        done++;
    }

    auto s = ORStringNew(isolate, builder);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(string_rfind, rfind,
               R"DOC(
@brief Return the codepoint index of the last occurrence of sub, or -1.

The index is measured in Unicode codepoints — consistent with `length` and
`substring` — so the result composes directly with `substring`, for ASCII and
multi-byte strings alike.

@param sub  The substring to search for.

@return An Int codepoint index, or -1 if not found.

@panic TypeError  When `sub` is not a String.

@see find, substring

@example
    "hello".rfind("l")     // 3
    "héllo".rfind("l")     // 3  (codepoints, not bytes)
    "hello".rfind("x")     // -1
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("sub", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *sub = (ORString *) argv[1];

    const auto idx = support::RSearch(STR_BUF(self), STR_LEN(self), STR_BUF(sub), STR_LEN(sub));

    const IntegerUnderlying cp_idx = idx < 0
                                         ? -1
                                         : (IntegerUnderlying) StrByteToCodepoint(self, (MSize) idx);

    auto result = IntNew(O_GET_ISOLATE(self), cp_idx);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(string_rstrip, rstrip,
               R"DOC(
@brief Return a copy with trailing characters in `chars` removed.

When `chars` is omitted the default ASCII whitespace set is used
(` `, `\t`, `\n`, `\v`, `\f`, `\r`). When provided, `chars` is treated
as a set of codepoints: trailing codepoints of self that appear in
`chars` are removed (full Unicode-aware, multi-byte safe).

@param chars?  Set of codepoints to strip. Defaults to ASCII whitespace.

@return A new String, or self if no stripping is needed.

@panic TypeError  When `chars` is not a String.

@see lstrip, strip

@example
    "  hello  ".rstrip()       // "  hello"
    "helloxx".rstrip("x")      // "hello"
)DOC", 1, "chars", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("chars", true, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *chars = O_IS_SENTINEL(argv[1]) ? nullptr : (const ORString *) argv[1];
    auto *isolate = O_GET_ISOLATE(_func);

    if (STR_LEN(self) == 0)
        return HOObject(argv[0]);

    MSize end = STR_LEN(self);
    while (end > 0) {
        const MSize cp_start = StrLastCodepointStart(self, end);
        const auto clen = StrUtf8LeadByteCount(STR_BUF(self)[cp_start]);
        if (clen == 0 || cp_start + clen != end)
            break; // defensive: malformed

        if (!CodepointInChars(STR_BUF(self) + cp_start, clen, chars))
            break;

        end = cp_start;
    }

    if (end == STR_LEN(self))
        return HOObject(argv[0]);

    if (end == 0)
        return HOObject(ORStringNew(isolate, "", 0));

    auto s = ORStringNew(isolate, STR_BUF(self), end);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(string_split, split,
               R"DOC(
@brief Split the string and return a list of substrings.

When called without a separator (or with nil), splits on runs of ASCII whitespace
(` `, `\t`, `\n`, `\v`, `\f`, `\r`) and drops empty leading/trailing/interior segments.

When called with an explicit separator, performs exact (non-overlapping) matching
and preserves empty leading/trailing segments; "aaaa".split("aa") → ["", "", ""].

@param sep?  The separator string, or nil for whitespace split. Must not be empty
             when provided.
@param max?  Maximum number of splits. -1 (or omitted) splits at every match.
             Applies to both the separator and the whitespace form.

@return A new List of Strings.

@panic TypeError   When `sep` is neither a String nor nil, or `max` is not an Int.
@panic ValueError  When `sep` is the empty string.

@see splitlines, replace, find

@example
    "a,b,c".split(sep=",")          // ["a", "b", "c"]
    "a,b,c,d".split(sep=",", max=2) // ["a", "b", "c,d"]
    "  a  b  c ".split(max=1)       // ["a", "b  c "]
)DOC", 1, "sep, max", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("sep", true, InstanceType::STRING, InstanceType::NIL),
                   PCHECK_DEF("max", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    MSSize max = -1;
    if (!O_IS_SENTINEL(argv[2])) {
        if (!NumberExtract(argv[2], (IntegerUnderlying &) max))
            return {};
    }

    // Whitespace split when sep is omitted or explicitly nil.
    if (O_IS_SENTINEL(argv[1]) || O_IS_NIL(argv[1])) {
        auto list = ORStringSplitWhitespace(isolate, self, max);
        if (!list)
            return {};

        return HOObject(std::move(list));
    }

    const auto *sep = (ORString *) argv[1];

    if (STR_LEN(sep) == 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "sep must be a non-empty string");

        return {};
    }

    auto list = ORStringSplit(isolate, self, sep, max);
    if (!list)
        return {};

    return HOObject(std::move(list));
}

RUNTIME_METHOD(string_splitlines, splitlines,
               R"DOC(
@brief Split the string on line terminators and return a list of lines.

By default, recognizes the three universal line terminators:
  - `\n`   (LF)
  - `\r\n` (CRLF)
  - `\r`   (lone CR)

A trailing single terminator is swallowed, so `"a\n"` → `["a"]`; consecutive
terminators produce empty lines, so `"a\n\nb"` → `["a", "", "b"]`. Empty input
returns an empty list.

@param keepends?   When true, retain the terminator in each returned line
                   (defaults to false).
@param universal?  When true, treat `\r\n` and `\r` as terminators in addition
                   to `\n` (defaults to true). Disable for strict LF-only
                   splitting.

@return A new List of Strings.

@panic TypeError  When `keepends` or `universal` is neither a Bool.

@see split

@example
    "a\nb\nc".splitlines()                                  // ["a", "b", "c"]
    "a\r\nb\rc".splitlines()                                // ["a", "b", "c"]
    "a\nb\n".splitlines()                                   // ["a", "b"]
    "a\nb".splitlines(keepends=true)                        // ["a\n", "b"]
    "a\r\nb".splitlines(keepends=false, universal=false)    // ["a\r", "b"]
)DOC", 1, "keepends, universal", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("keepends", true, InstanceType::BOOLEAN),
                   PCHECK_DEF("universal", true, InstanceType::BOOLEAN));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    // Defaults: keepends = false, universal = true
    const bool keepends = O_IS_SENTINEL(argv[1]) ? false : OBOOL_TO_BOOL(argv[1]);
    const bool universal = O_IS_SENTINEL(argv[2]) ? true : OBOOL_TO_BOOL(argv[2]);

    auto list = support::SplitLines(isolate, STR_BUF(self), STR_LEN(self),
                                    [](orbiter::Isolate *isolate, const unsigned char *buffer,
                                       const MSize length) {
                                        return ORStringNew(isolate, buffer, length);
                                    }, keepends, universal);
    if (!list)
        return {};

    return HOObject(std::move(list));
}

RUNTIME_METHOD(string_starts_with, starts_with,
               R"DOC(
@brief Return true if the string starts with the given prefix.

@param prefix  The prefix to check.

@return true if start with the given prefix, false otherwise.

@panic TypeError  When `prefix` is not a String.

@see ends_with

@example
    "hello".starts_with("hel")    // true
    "hello".starts_with("llo")    // false
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("prefix", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *prefix = (ORString *) argv[1];

    if (STR_LEN(prefix) > STR_LEN(self))
        return HOObject((OObject *) BOOL_TO_OBOOL(false));

    const bool ok = memcmp(STR_BUF(self), STR_BUF(prefix), STR_LEN(prefix)) == 0;

    return HOObject((OObject *) BOOL_TO_OBOOL(ok));
}

RUNTIME_METHOD(string_str, str,
               R"DOC(
@brief Return the string itself.

For String, `str()` is the identity: the receiver is returned unchanged
(same object, no copy).

@return The receiver.

@see repr

@example
    "hello".str()    // "hello"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    return HOObject(argv[0]);
}

RUNTIME_METHOD(string_strip, strip,
               R"DOC(
@brief Return a copy with leading and trailing characters in `chars` removed.

When `chars` is omitted the default ASCII whitespace set is used
(` `, `\t`, `\n`, `\v`, `\f`, `\r`). When provided, `chars` is treated
as a set of codepoints (full Unicode-aware, multi-byte safe).

@param chars?  Set of codepoints to strip. Defaults to ASCII whitespace.

@return A new String, or self if no stripping is needed.

@panic TypeError  When `chars` is not a String.

@see lstrip, rstrip

@example
    "  hello  ".strip()        // "hello"
    "xxhellox".strip("x")      // "hello"
)DOC", 1, "chars", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("chars", true, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *chars = O_IS_SENTINEL(argv[1]) ? nullptr : (const ORString *) argv[1];
    auto *isolate = O_GET_ISOLATE(_func);

    MSize start = 0;
    while (start < STR_LEN(self)) {
        const auto clen = StrUtf8LeadByteCount(STR_BUF(self)[start]);
        if (clen == 0 || start + clen > STR_LEN(self))
            break;

        if (!CodepointInChars(STR_BUF(self) + start, clen, chars))
            break;

        start += clen;
    }

    if (start == STR_LEN(self))
        return HOObject(ORStringNew(isolate, "", 0));

    MSize end = STR_LEN(self);
    while (end > start) {
        const MSize cp_start = StrLastCodepointStart(self, end);
        if (cp_start < start)
            break;

        const auto clen = StrUtf8LeadByteCount(STR_BUF(self)[cp_start]);
        if (clen == 0 || cp_start + clen != end)
            break;

        if (!CodepointInChars(STR_BUF(self) + cp_start, clen, chars))
            break;

        end = cp_start;
    }

    if (start == 0 && end == STR_LEN(self))
        return HOObject(argv[0]);

    auto s = ORStringNew(isolate, STR_BUF(self) + start, end - start);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(string_substring, substring,
               R"DOC(
@brief Return the substring spanning a half-open range of codepoints.

`start` and `end` are measured in Unicode codepoints (not bytes), with
`end` exclusive: `s.substring(a, b)` returns the codepoints in the range
`[a, b)`. The result is a fresh String; the receiver is left unchanged.

Because the position is given in codepoints, this is O(1) for ASCII
strings — where one codepoint is one byte — but O(n) for strings that
hold multi-byte codepoints, since the byte offsets of `start` and `end`
must be located by scanning from the front. Use `is_ascii` to tell the
fast and slow cases apart when it matters.

@param start  Index of the first codepoint to include (0-based).
@param end    Index one past the last codepoint to include.

@return A new String with the codepoints in [start, end); empty when
        start == end.

@panic TypeError   When `start` or `end` is not a Number.
@panic ValueError  When the bounds fall outside 0 <= start <= end <= length().

@see length, is_ascii, split

@example
    "hello".substring(1, 4)     // "ell"
    "héllo".substring(0, 2)     // "hé"
    "hello".substring(2, 2)     // ""
)DOC", 3, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("start", false, InstanceType::NUMBER),
                   PCHECK_DEF("end", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying start;
    IntegerUnderlying end;
    if (!NumberExtract(argv[1], start) || !NumberExtract(argv[2], end))
        return {};

    if (start < 0 || end < start || (MSize) end > self->cp_length) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "substring bounds out of range");

        return {};
    }

    // Translate codepoint indices into byte offsets. ASCII is a 1:1 mapping,
    // so we can index the buffer directly; otherwise walk codepoint by
    // codepoint (the range was validated above, so the scan stays in bounds).
    MSize start_byte;
    MSize end_byte;

    if (self->kind == StringKind::ASCII) {
        start_byte = (MSize) start;
        end_byte = (MSize) end;
    } else {
        MSize byte = 0;
        MSize cp = 0;

        while (cp < (MSize) start) {
            byte += StrUtf8LeadByteCount(STR_BUF(self)[byte]);

            cp++;
        }

        start_byte = byte;

        while (cp < (MSize) end) {
            byte += StrUtf8LeadByteCount(STR_BUF(self)[byte]);

            cp++;
        }

        end_byte = byte;
    }

    auto s = ORStringNew(isolate, STR_BUF(self) + start_byte, end_byte - start_byte);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(string_upper, upper,
               R"DOC(
@brief Return a copy of the string with ASCII letters converted to uppercase.

Non-ASCII bytes are passed through unchanged.

@return A new String.

@see lower

@example
    "Hello World".upper()    // "HELLO WORLD"
    "abc123".upper()         // "ABC123"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    auto *result = MkStringContainer(isolate, STR_LEN(self), true);
    if (result == nullptr)
        return {};

    for (MSize i = 0; i < STR_LEN(self); i++)
        STR_BUF(result)[i] = STR_BUF(self)[i] < 0x80u
                                 ? (unsigned char) std::toupper(STR_BUF(self)[i])
                                 : STR_BUF(self)[i];

    STR_BUF(result)[STR_LEN(self)] = '\0';

    result->kind = self->kind;
    result->cp_length = self->cp_length;

    isolate->gc->Track((OObject *) result, false);

    return HOObject((OObject *) result);
}

constexpr FunctionDef string_methods[] = {
    string_at,
    string_byte_length,
    string_byte_substring,
    string_contains,
    string_count,
    string_ends_with,
    string_find,
    string_is_ascii,
    string_length,
    string_lower,
    string_lstrip,
    string_next_codepoint,
    string_replace,
    string_rfind,
    string_rstrip,
    string_split,
    string_splitlines,
    string_starts_with,
    string_str,
    string_strip,
    string_substring,
    string_upper,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::ORStringTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) StrDtor;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.compare = StrCompare;
    ops.contains = StrContains;
    ops.equal = StrOpEqual;
    ops.add = StrAdd;
    ops.mul = StrMul;
    ops.mod = StrMod;
    ops.get_iter = StrGetIter;
    ops.get_riter = StrGetReverseIter;
    ops.to_bool = StrToBool;
    ops.to_string = StrToString;
    ops.to_repr = (ToStrFn) StrToRepr;
    ops.to_native = (ToNativeType) StrToNative;
    ops.hash = StrHashOp;

    return TIPropertyAdd(self, string_methods, PropertyFlag::IS_PUBLIC);
}

int orbiter::datatype::ORStringCompare(const ORString *left, const ORString *right) {
    if (left != right && right != nullptr) {
        const auto compare = strncmp((const char *) STR_BUF(left),
                                     (const char *) STR_BUF(right),
                                     std::min(STR_LEN(left),STR_LEN(right)));

        if (compare == 0) {
            if (STR_LEN(left) < STR_LEN(right)) return -1;
            if (STR_LEN(left) > STR_LEN(right)) return 1;
        }

        return compare;
    }

    return 0;
}

int orbiter::datatype::ORStringCompare(const ORString *left, const char *right, const MSize length) {
    const auto compare = strncmp((const char *) STR_BUF(left), right, std::min(STR_LEN(left), length));

    if (compare == 0) {
        if (STR_LEN(left) < length) return -1;
        if (STR_LEN(left) > length) return 1;
    }

    return compare;
}

int orbiter::datatype::ORStringCompare(const char *left, const ORString *right, const MSize length) {
    const auto compare = strncmp(left, (const char *) STR_BUF(right), std::min(STR_LEN(right), length));

    if (compare == 0) {
        if (length < STR_LEN(right)) return -1;
        if (length > STR_LEN(right)) return 1;
    }

    return compare;
}

bool orbiter::datatype::ORStringContains(const ORString *self, const ORString *sub) {
    return support::Search(STR_BUF(self), STR_LEN(self), STR_BUF(sub), STR_LEN(sub)) >= 0;
}

bool orbiter::datatype::ORStringEndsWith(const ORString *self, const ORString *suffix) noexcept {
    if (STR_LEN(suffix) > STR_LEN(self))
        return false;

    const auto offset = STR_LEN(self) - STR_LEN(suffix);
    return memcmp(STR_BUF(self) + offset, STR_BUF(suffix), STR_LEN(suffix)) == 0;
}

HORString orbiter::datatype::ORStringFormat(Isolate *isolate, const char *format, ...) {
    va_list args;

    va_start(args, format);
    auto str = ORStringFormat(isolate, format, args);
    va_end(args);

    return str;
}

HORString orbiter::datatype::ORStringFormat(Isolate *isolate, const char *format, va_list args) {
    va_list args2;

    va_copy(args2, args);
    const int size = vsnprintf(nullptr, 0, format, args2);
    va_end(args2);

    auto *str = MkStringContainer(isolate, size, true);
    if (str == nullptr)
        return {};

    vsnprintf((char *) STR_BUF(str), size + 1, format, args);
    str->cp_length = size;
    str->kind = StringKind::ASCII;

    O_GC_TRACK_RETURN(isolate, str, false);
}

HORString orbiter::datatype::ORStringIntern(Isolate *isolate, const unsigned char *string, const MSize length) {
    auto *gst = (GST *) isolate->primitive[(int) InstanceType::STRING]->aux.data;
    assert(gst != nullptr);

    StringEntry *entry;

    std::shared_lock shared_lock(gst->lock);

    auto ok = gst->map.Lookup(
        StrRawEqual,
        StrHash,
        string,
        length,
        &entry
    );
    if (ok == LookupResult::OK)
        return HORString(entry->value);

    shared_lock.unlock();

    std::unique_lock lock(gst->lock);

    ok = gst->map.Lookup(
        StrRawEqual,
        StrHash,
        string,
        length,
        &entry
    );
    if (ok == LookupResult::OK)
        return HORString(entry->value);

    if ((entry = gst->map.AllocHEntry()) == nullptr)
        return {};

    StringBuilder builder(isolate);
    StringKind kind;
    MSize len;
    MSize cp_len;

    builder.Write(string, length, 0);

    auto *buffer = builder.BuildString(nullptr, &len, &cp_len, &kind);
    if (buffer == nullptr && len != 0) {
        gst->map.FreeHEntry(entry);

        assert(false);
        // TODO: ERROR!
    }

    auto str = ORStringNew(isolate, buffer, len, cp_len, kind);
    if (!str) {
        gst->map.FreeHEntry(entry);

        return {};
    }

    builder.Release();

    entry->key = str.get();
    entry->value = str.get();

    if (gst->map.Insert(entry) == LookupResult::OK) {
        O_FAST_INCREF(entry->key);
        O_FAST_INCREF(entry->value);

        str->intern = true;

        if (length == 0) {
            assert(gst->empty == nullptr);

            gst->empty = str.get();
        }

        return str;
    }

    gst->map.FreeHEntry(entry);

    return {};
}

HORString orbiter::datatype::ORStringNew(Isolate *isolate, unsigned char *string, const MSize length,
                                         const MSize cp_length, const StringKind kind) {
    assert(string[length] == '\0');

    auto *str = MkStringContainer(isolate, length, false);
    if (str != nullptr) {
        str->buffer = string;
        str->cp_length = cp_length;
        str->kind = kind;
    }

    O_GC_TRACK_RETURN(isolate, str, false);
}

HORString orbiter::datatype::ORStringNew(Isolate *isolate, const unsigned char *string, const MSize length) {
    if (string == nullptr || length == 0) {
        const auto *gst = (GST *) isolate->primitive[(int) InstanceType::STRING]->aux.data;

        if (gst->empty != nullptr)
            return HORString(gst->empty);

        return ORStringIntern(isolate, string, length);
    }

    StringBuilder builder(isolate);
    StringKind kind;
    MSize len;
    MSize cp_len;

    builder.Write(string, length, 0);

    auto *buffer = builder.BuildString(nullptr, &len, &cp_len, &kind);
    if (buffer == nullptr && len != 0) {
        assert(false);
        // TODO: ERROR!
    }

    auto str = ORStringNew(isolate, buffer, len, cp_len, kind);
    if (str)
        builder.Release();

    return str;
}

HORString orbiter::datatype::ORStringNew(Isolate *isolate, StringBuilder &builder) {
    MSize len;
    MSize cp_len;
    StringKind kind;

    auto *buf = builder.BuildString(nullptr, &len, &cp_len, &kind);
    if (buf == nullptr)
        return {};

    if (len == 0) {
        const auto *gst = (GST *) isolate->primitive[(int) InstanceType::STRING]->aux.data;

        if (gst->empty != nullptr)
            return HORString(gst->empty);

        return ORStringIntern(isolate, (const unsigned char *) "", 0);
    }

    const auto s = ORStringNew(isolate, buf, len, cp_len, kind);
    if (!s)
        return {};

    builder.Release();

    return s;
}

HORString orbiter::datatype::ORStringNewHoldBuffer(Isolate *isolate, unsigned char *buffer, const MSize length) {
    assert(buffer[length] == '\0');

    auto *str = MkStringContainer(isolate, length, false);
    if (str != nullptr) {
        str->buffer = buffer;

        if (!StringInitKind(str)) {
            str->buffer = nullptr;

            isolate->gc->RawFree((OObject *) str, false);

            return {};
        }
    }

    O_GC_TRACK_RETURN(isolate, str, false);
}

HOType orbiter::datatype::ORStringTypeInit(Isolate *isolate) {
    memory::IsolateAllocator allocator(isolate);

    auto *gst = allocator.AllocObject<GST>(isolate);
    if (gst == nullptr)
        return {};

    if (!gst->map.Initialize()) {
        allocator.FreeObject(gst);

        return {};
    }

    auto string = MakeType(isolate, "String", InstanceType::STRING, sizeof(ORString) - sizeof(OObject), 22, 0);
    if (!string) {
        allocator.FreeObject(gst);

        return {};
    }

    string->aux.data = gst;
    string->aux.dtor = (TypeInfoAUXDtor) StrGSTDtor;

    return string;
}

MSize orbiter::datatype::ORStringHash(ORString *string) noexcept {
    if (string->hash != 0)
        return string->hash;

    string->hash = StrHash(STR_BUF(string), STR_LEN(string));

    return string->hash;
}

MSSize orbiter::datatype::ORStringRFind(const ORString *self, const ORString *sub) noexcept {
    return support::RSearch(STR_BUF(self), STR_LEN(self), STR_BUF(sub), STR_LEN(sub));
}

MSSize orbiter::datatype::ORStringRFind(const ORString *self, const char *sub) noexcept {
    return support::RSearch(STR_BUF(self), STR_LEN(self), (unsigned char *) sub, strlen(sub));
}

HList orbiter::datatype::ORStringSplit(Isolate *isolate, const ORString *self, const ORString *sep,
                                       const MSSize maxsplit) {
    return support::Split(isolate,
                          STR_BUF(self),
                          STR_LEN(self),
                          STR_BUF(sep),
                          STR_LEN(sep),
                          [](Isolate *iso, const unsigned char *buffer, const MSize length) {
                              return ORStringNew(iso, buffer, length);
                          },
                          maxsplit);
}

HList orbiter::datatype::ORStringSplitWhitespace(Isolate *isolate, const ORString *self, const MSSize maxsplit) {
    return support::SplitWhitespace(isolate,
                                    STR_BUF(self),
                                    STR_LEN(self),
                                    [](Isolate *iso, const unsigned char *buffer, const MSize length) {
                                        return ORStringNew(iso, buffer, length);
                                    },
                                    maxsplit);
}
