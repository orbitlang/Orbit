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

static int StrCompare(const OObject *left, const OObject *right) {
    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::STRING))
        return 0;

    return ORStringCompare((const ORString *) left, (const ORString *) right);
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

    if (STR_LEN(suffix) > STR_LEN(self))
        return HOObject((OObject *) BOOL_TO_OBOOL(false));

    const auto offset = STR_LEN(self) - STR_LEN(suffix);
    const bool ok = memcmp(STR_BUF(self) + offset, STR_BUF(suffix), STR_LEN(suffix)) == 0;

    return HOObject((OObject *) BOOL_TO_OBOOL(ok));
}

RUNTIME_METHOD(string_find, find,
               R"DOC(
@brief Return the byte offset of the first occurrence of sub, or -1.

@param sub  The substring to search for.

@return An Int offset, or -1 if not found.

@panic TypeError  When `sub` is not a String.

@see rfind, contains

@example
    "hello".find("ll")    // 2
    "hello".find("x")     // -1
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("sub", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *sub = (ORString *) argv[1];

    const auto idx = support::Search(STR_BUF(self), STR_LEN(self), STR_BUF(sub), STR_LEN(sub));

    auto result = IntNew(O_GET_ISOLATE(self), (IntegerUnderlying) idx);
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
@brief Return a copy with leading whitespace removed.

Whitespace is determined by std::isspace (ASCII only).

@return A new String, or self if no stripping is needed.

@see rstrip, strip

@example
    "  hello  ".lstrip()    // "hello  "
    "hello".lstrip()        // "hello"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    MSize start = 0;
    while (start < STR_LEN(self) && std::isspace(STR_BUF(self)[start]))
        start++;

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

@param old  Substring to replace.
@param new  Replacement string.

@return A new String.

@panic TypeError  When `old` or `new` is not a String.

@see find, split

@example
    "hello".replace("l", "r")       // "herro"
    "aabbcc".replace("bb", "XX")    // "aaXXcc"
)DOC", 3, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("old", false, InstanceType::STRING),
                   PCHECK_DEF("new", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *old_str = (ORString *) argv[1];
    const auto *new_str = (ORString *) argv[2];
    auto *isolate = O_GET_ISOLATE(_func);

    if (STR_LEN(old_str) == 0)
        return HOObject(argv[0]);

    StringBuilder builder(isolate);
    MSize pos = 0;

    while (pos < STR_LEN(self)) {
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
    }

    MSize len;
    MSize cp_len;
    StringKind kind;
    auto *buf = builder.BuildString(nullptr, &len, &cp_len, &kind);
    if (buf == nullptr)
        return {};

    if (len == 0)
        return HOObject(ORStringNew(isolate, "", 0));

    auto s = ORStringNew(isolate, buf, len, cp_len, kind);
    if (!s)
        return {};

    builder.Release();

    return HOObject(std::move(s));
}

RUNTIME_METHOD(string_rfind, rfind,
               R"DOC(
@brief Return the byte offset of the last occurrence of sub, or -1.

@param sub  The substring to search for.

@return An Int offset, or -1 if not found.

@panic TypeError  When `sub` is not a String.

@see find

@example
    "hello".rfind("l")    // 3
    "hello".rfind("x")    // -1
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("sub", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    const auto *sub = (ORString *) argv[1];

    const auto idx = support::RSearch(STR_BUF(self), STR_LEN(self), STR_BUF(sub), STR_LEN(sub));

    auto result = IntNew(O_GET_ISOLATE(self), idx);
    if (!result)
        return {};

    return HOObject(std::move(result));
}

RUNTIME_METHOD(string_rstrip, rstrip,
               R"DOC(
@brief Return a copy with trailing whitespace removed.

Whitespace is determined by std::isspace (ASCII only).

@return A new String, or self if no stripping is needed.

@see lstrip, strip

@example
    "  hello  ".rstrip()    // "  hello"
    "hello".rstrip()        // "hello"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    if (STR_LEN(self) == 0)
        return HOObject(argv[0]);

    auto end = (MSSize) STR_LEN(self) - 1;
    while (end >= 0 && std::isspace(STR_BUF(self)[(MSize) end]))
        end--;

    if (end == (MSSize) STR_LEN(self) - 1)
        return HOObject(argv[0]);

    if (end < 0)
        return HOObject(ORStringNew(isolate, "", 0));

    auto s = ORStringNew(isolate, STR_BUF(self), (MSize) end + 1);
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

@return A new List of Strings.

@panic TypeError   When `sep` is neither a String nor nil.
@panic ValueError  When `sep` is the empty string.

@see splitlines, replace, find

@example
    "a,b,c".split(sep=",")        // ["a", "b", "c"]
    "hello".split(sep="l")        // ["he", "", "o"]
    "  hello   world ".split()   // ["hello", "world"]
    "a\tb\nc".split()         // ["a", "b", "c"]
)DOC", 1, "sep", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::STRING),
                   PCHECK_DEF("sep", true, InstanceType::STRING, InstanceType::NIL));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    // Whitespace split when sep is omitted or explicitly nil
    if (O_IS_SENTINEL(argv[1]) || O_IS_NIL(argv[1])) {
        auto list = support::SplitWhitespace<ORString>(isolate, STR_BUF(self), STR_LEN(self), ORStringNew);
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

    auto list = support::Split<ORString>(isolate, STR_BUF(self), STR_LEN(self), STR_BUF(sep),
                                         STR_LEN(sep), ORStringNew);
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

    auto list = support::SplitLines<ORString>(isolate, STR_BUF(self), STR_LEN(self), ORStringNew, keepends, universal);
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

RUNTIME_METHOD(string_strip, strip,
               R"DOC(
@brief Return a copy with leading and trailing whitespace removed.

Whitespace is determined by std::isspace (ASCII only).

@return A new String, or self if no stripping is needed.

@see lstrip, rstrip

@example
    "  hello  ".strip()    // "hello"
    "hello".strip()        // "hello"
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::STRING));
    PCHECK_CHECK(params);

    const auto *self = (ORString *) argv[0];
    auto *isolate = O_GET_ISOLATE(_func);

    MSize start = 0;
    while (start < STR_LEN(self) && std::isspace(STR_BUF(self)[start]))
        start++;

    if (start == STR_LEN(self))
        return HOObject(ORStringNew(isolate, "", 0));

    auto end = STR_LEN(self);
    while (end > start && std::isspace(STR_BUF(self)[end - 1]))
        end--;

    if (start == 0 && end == STR_LEN(self))
        return HOObject(argv[0]);

    auto s = ORStringNew(isolate, STR_BUF(self) + start, end - start);
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
    string_contains,
    string_count,
    string_ends_with,
    string_find,
    string_is_ascii,
    string_length,
    string_lower,
    string_lstrip,
    string_replace,
    string_rfind,
    string_rstrip,
    string_split,
    string_splitlines,
    string_starts_with,
    string_strip,
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
    ops.equal = (EqualFn) StrEqual;
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

MSize orbiter::datatype::ORStringHash(ORString *string) {
    if (string->hash != 0)
        return string->hash;

    string->hash = StrHash(STR_BUF(string), STR_LEN(string));

    return string->hash;
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

    auto string = MakeType(isolate, "String", InstanceType::STRING, sizeof(ORString) - sizeof(OObject), 16, 0);
    if (!string) {
        allocator.FreeObject(gst);

        return {};
    }

    string->aux.data = gst;
    string->aux.dtor = (TypeInfoAUXDtor) StrGSTDtor;

    return string;
}
