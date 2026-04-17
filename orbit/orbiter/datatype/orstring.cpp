// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <cstdarg>

#include <orbit/util/hash.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/hashmap.h>

using namespace orbiter::datatype;

#define STR_BUF(str) ((str)->buffer)
#define STR_LEN(str) ((str)->length)

bool StrEqual(const ORString *left, const ORString *right) {
    if (left == right)
        return true;

    if (!O_IS_TYPE(right, InstanceType::STRING))
        return false;

    if (STR_LEN(left) != STR_LEN(right))
        return false;

    return ORStringEqual(left, right);
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

ORString *MkStringContainer(orbiter::Isolate *isolate, const MSize len, bool mkbuf) {
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

bool orbiter::datatype::ORStringTypeSetup(TypeInfo *self) {
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = (EqualFn) StrEqual;
    ops.to_native = (ToNativeType) StrToNative;

    return true;
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
    // TODO: IMPL THIS!
    return ORStringNew(isolate, string, length);
}

HORString orbiter::datatype::ORStringNew(Isolate *isolate, unsigned char *string, const MSize length,
                                         const MSize cp_length,
                                         const StringKind kind) {
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

    // TODO: do not release buffer of size zero!

    auto str = ORStringNew(isolate, buffer, len, cp_len, kind);
    if (str)
        builder.Release();

    return str;
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

    auto hash = fnv1_hash(STR_BUF(string), STR_LEN(string));
    if (hash == 0 || hash == HASH_ERROR)
        hash = 1;

    string->hash = hash;

    return hash;
}

HOType orbiter::datatype::ORStringTypeInit(Isolate *isolate) {
    auto string = MakeType(isolate, "String", InstanceType::STRING, sizeof(ORString) - sizeof(OObject), 0, 0);
    return string;
}
