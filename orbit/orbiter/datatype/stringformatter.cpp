// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cctype>

#include <orbit/orbiter/datatype/bytes.h>
#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/stringbuilder.h>
#include <orbit/orbiter/datatype/tuple.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

#include <orbit/orbiter/datatype/stringformatter.h>

using namespace orbiter::datatype;

namespace {
    /** Count decimal digits needed to represent @p value in the given @p base. */
    int DigitCountS(IntegerUnderlying value, const int base) {
        if (value == 0)
            return 1;

        if (value < 0)
            value = -value;

        int count = 0;
        while (value) {
            count++;
            value /= base;
        }

        return count;
    }

    /** Count decimal digits needed to represent @p value in the given @p base. */
    int DigitCountU(UIntegerUnderlying value, const int base) {
        if (value == 0)
            return 1;

        int count = 0;
        while (value) {
            count++;
            value /= base;
        }

        return count;
    }

    /**
     * @brief Compute the byte length of the first @p max_cps UTF-8 codepoints.
     *
     * Walks @p buf without validating the encoding.  If the buffer ends before
     * @p max_cps codepoints are consumed, returns the total byte length.
     */
    MSize Utf8BytesForCps(const unsigned char *buf, const MSize byte_len, const MSize max_cps) {
        MSize i = 0;
        MSize cps = 0;

        while (i < byte_len && cps < max_cps) {
            const unsigned char c = buf[i];

            if (c < 0x80u)
                i += 1;
            else if (c < 0xE0u)
                i += 2;
            else if (c < 0xF0u)
                i += 3;
            else
                i += 4;

            ++cps;
        }

        return i;
    }
}

// =============================================================================
// Private methods
// =============================================================================

OObject *StringFormatter::NextArg() {
    if (O_IS_OBJECT(this->fmt_.args) && O_IS_TYPE(this->fmt_.args, InstanceType::TUPLE)) {
        const auto *tp = (const Tuple *) this->fmt_.args;
        this->fmt_.args_length = tp->length;

        if (this->fmt_.args_index < this->fmt_.args_length)
            return tp->objects[this->fmt_.args_index++];
    } else {
        this->fmt_.args_length = 1;

        if (this->fmt_.args_index++ == 0)
            return this->fmt_.args;
    }

    ErrorSet(this->isolate_,
             TypeError::Details[TypeError::ID],
             nullptr,
             "not enough arguments for format string");

    this->failed_ = true;

    return nullptr;
}

MSSize StringFormatter::FormatBytes() {
    const OObject *obj = this->NextArg();
    if (obj == nullptr)
        return -1;

    // Bytes-mode `%s`: consume the argument as raw bytes instead of routing
    // it through ToString.  Accept both Bytes (the canonical case) and
    // String (whose backing buffer is also byte-addressable) so callers
    // can mix the two without a manual conversion.  Anything else is a
    // hard TypeError.
    const unsigned char *buf = nullptr;
    MSize len = 0;

    if (O_IS_OBJECT(obj)) {
        if (O_IS_TYPE(obj, InstanceType::BYTES)) {
            const auto *b = (const Bytes *) obj;
            buf = b->shared->buffer + b->start;
            len = b->length;
        } else if (O_IS_TYPE(obj, InstanceType::STRING)) {
            buf = ((const ORString *) obj)->buffer;
            len = ((const ORString *) obj)->length;
        }
    }

    if (buf == nullptr) {
        if (!this->failed_) {
            ErrorSet(this->isolate_,
                     TypeError::Details[TypeError::ID],
                     nullptr,
                     "%%s in bytes format requires a Bytes or String argument");

            this->failed_ = true;
        }

        return -1;
    }

    // Precision caps the number of bytes to emit (raw byte count — unlike
    // FormatString we don't translate to codepoints, because there is no
    // codepoint structure in a Bytes value).
    if (this->fmt_.prec > -1 && len > (MSize) this->fmt_.prec)
        len = (MSize) this->fmt_.prec;

    // Compute width-driven padding identically to FormatString.
    MSSize padding = 0;
    if (this->fmt_.width > 0 && (MSize) this->fmt_.width > len)
        padding = (MSSize) ((MSize) this->fmt_.width - len);

    // Right-justify: emit left padding before the content.
    if (padding > 0 && !ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::LJUST)) {
        if (this->WriteRepeat(' ', (int) padding) < 0)
            return -1;

        padding = 0;
    }

    if (this->Write(buf, len, 0) < 0)
        return -1;

    // Any remaining `padding` is consumed by the caller as LJUST tail
    // padding — same protocol FormatString follows.
    return padding;
}

MSSize StringFormatter::FormatString() {
    const OObject *obj = this->NextArg();
    if (obj == nullptr)
        return -1;

    const auto str_handle = ToString(this->isolate_, obj);

    if (!str_handle ||
        !O_IS_OBJECT(str_handle.get()) ||
        !O_IS_TYPE(str_handle.get(), InstanceType::STRING)) {
        if (!this->failed_) {
            ErrorSet(this->isolate_,
                     TypeError::Details[TypeError::ID],
                     nullptr,
                     "%%s: could not convert argument to string");

            this->failed_ = true;
        }

        return -1;
    }

    const auto *str = (const ORString *) str_handle.get();
    MSize str_len = str->length;

    // Apply precision (maximum codepoints to print)
    if (this->fmt_.prec > -1 && str_len > (MSize) this->fmt_.prec) {
        str_len = (str->kind != StringKind::ASCII)
                      ? Utf8BytesForCps(str->buffer, str->length, (MSize) this->fmt_.prec)
                      : (MSize) this->fmt_.prec;
    }

    // Compute how many padding spaces are needed for field-width alignment
    MSSize padding = 0;
    if (this->fmt_.width > 0 && (MSize) this->fmt_.width > str_len)
        padding = (MSSize) ((MSize) this->fmt_.width - str_len);

    // Right-justify: write left-padding before the string
    if (padding > 0 && !ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::LJUST)) {
        if (this->WriteRepeat(' ', (int) padding) < 0)
            return -1;

        padding = 0; // already handled
    }

    if (this->Write(str->buffer, str_len, 0) < 0)
        return -1;

    // Return remaining padding for Format() to use as right-padding (LJUST)
    return padding;
}

MSSize StringFormatter::Write(const unsigned char *buffer, const MSize length, int overalloc) {
    if (!this->BufferResize(length + (MSize) overalloc))
        return -1;

    const unsigned char *before = this->output_.cursor;

    std::memcpy(this->output_.cursor, buffer, length);

    this->output_.cursor += length;

    return this->output_.cursor - before;
}

MSSize StringFormatter::WriteRepeat(char ch, const int times) {
    if (times <= 0)
        return 0;

    if (!this->BufferResize((MSize) times))
        return -1;

    for (int i = 0; i < times; i++)
        *this->output_.cursor++ = (unsigned char) ch;

    return times;
}

bool StringFormatter::BufferResize(const MSize length) {
    MSize cap = 0;
    MSize len = 0;

    if (this->output_.buffer != nullptr) {
        cap = (MSize) (this->output_.end - this->output_.buffer);
        len = (MSize) (this->output_.cursor - this->output_.buffer);
    }

    // Reserve one extra byte for the null terminator when computing headroom
    const MSize available = (cap > 0) ? cap - 1 : 0;

    if (length == 0 || len + length <= available)
        return true;

    // Initial allocation: add one byte for '\0'
    const MSize extra = (this->output_.buffer == nullptr) ? length + 1 : length;

    auto *tmp = this->allocator_.realloc<unsigned char>(this->output_.buffer, cap + extra);
    if (tmp == nullptr) {
        this->failed_ = true;

        return false;
    }

    this->output_.buffer = tmp;
    this->output_.cursor = tmp + len;
    this->output_.end = tmp + (cap + extra);

    return true;
}

bool StringFormatter::FormatSpecifier() {
    const unsigned char op = *this->fmt_.cursor;
    MSSize result = -1;

    switch (op) {
        case 's':
            result = this->string_as_bytes_ ? this->FormatBytes() : this->FormatString();
            break;
        case 'b':
            result = this->FormatInteger(2, false, false);
            break;
        case 'B':
            result = this->FormatInteger(2, false, true);
            break;
        case 'o':
            result = this->FormatInteger(8, false, false);
            break;
        case 'i':
        case 'd':
            this->fmt_.flags &= ~FormatFlags::ALT;
            result = this->FormatInteger(10, false, false);
            break;
        case 'u':
            this->fmt_.flags &= ~FormatFlags::ALT;
            result = this->FormatInteger(10, true, false);
            break;
        case 'x':
            result = this->FormatInteger(16, true, false);
            break;
        case 'X':
            result = this->FormatInteger(16, true, true);
            break;
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
            result = this->FormatDecimal(op);
            break;
        case 'c':
            result = this->FormatChar();
            break;
        default: {
            const char safe_op = (op > 127 || op < 32) ? '?' : (char) op;

            ErrorSet(this->isolate_,
                     ValueError::Details[ValueError::ID],
                     nullptr,
                     "unsupported format character '%c' (0x%x)",
                     safe_op,
                     (unsigned) op);

            this->failed_ = true;

            break;
        }
    }

    this->fmt_.cursor++;

    if (result < 0)
        return false;

    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::LJUST))
        return this->WriteRepeat(' ', (int) result) >= 0;

    return true;
}

bool StringFormatter::NextSpecifier() {
    const unsigned char *base = this->fmt_.cursor;
    int index = 0;

    while ((base + index) < this->fmt_.end) {
        if (*(base + index++) == '%') {
            if ((base + index) >= this->fmt_.end) {
                ErrorSet(this->isolate_,
                         ValueError::Details[ValueError::ID],
                         nullptr,
                         "incomplete format specifier");

                this->failed_ = true;

                return false;
            }

            if (*(base + index) != '%') {
                // Found a real specifier: skip the '%' and stop
                this->fmt_.nspec++;
                this->fmt_.cursor++;

                index--;

                break;
            }

            // '%%' → write a single '%'
            if (this->Write(base, (MSize) index, 0) < 0)
                return false;

            this->fmt_.cursor += index + 1;

            base = this->fmt_.cursor;

            index = 0;
        }
    }

    // Flush any remaining literal text
    if (this->Write(base, (MSize) index, 0) < 0)
        return false;

    this->fmt_.cursor += index;

    return this->fmt_.cursor != this->fmt_.end;
}

bool StringFormatter::ParseOption() {
    // Reset per-specifier state
    this->fmt_.flags = FormatFlags::NONE;
    this->fmt_.width = 0;
    this->fmt_.prec = -1;

    // --- Flags ---
    while (*this->fmt_.cursor != '\0') {
        switch (*this->fmt_.cursor) {
            case '-': this->fmt_.flags |= FormatFlags::LJUST;
                break;
            case '+': this->fmt_.flags |= FormatFlags::SIGN;
                break;
            case ' ': this->fmt_.flags |= FormatFlags::BLANK;
                break;
            case '#': this->fmt_.flags |= FormatFlags::ALT;
                break;
            case '0': this->fmt_.flags |= FormatFlags::ZERO;
                break;
            default: goto PARSE_WIDTH;
        }
        this->fmt_.cursor++;
    }

PARSE_WIDTH:
    // '-' overrides '0'
    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::LJUST) &&
        ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::ZERO))
        this->fmt_.flags &= ~FormatFlags::ZERO;

    // --- Width ---
    if (*this->fmt_.cursor == '*') {
        this->fmt_.cursor++;

        if (!this->ParseStarOption(false))
            return false;
    } else {
        while (std::isdigit(*this->fmt_.cursor)) {
            this->fmt_.width = this->fmt_.width * 10 + (*this->fmt_.cursor - '0');
            this->fmt_.cursor++;
        }
    }

    // --- Precision ---
    if (*this->fmt_.cursor == '.') {
        this->fmt_.cursor++;

        if (*this->fmt_.cursor == '*') {
            this->fmt_.cursor++;
            if (!this->ParseStarOption(true))
                return false;
        } else {
            this->fmt_.prec = 0;

            while (std::isdigit(*this->fmt_.cursor)) {
                this->fmt_.prec = this->fmt_.prec * 10 + (*this->fmt_.cursor - '0');
                this->fmt_.cursor++;
            }
        }
    }

    return true;
}

bool StringFormatter::ParseStarOption(const bool prec) {
    const OObject *obj = this->NextArg();
    if (obj == nullptr)
        return false;

    IntegerUnderlying opt;
    if (!NumberExtract(obj, opt)) {
        ErrorSetWithObjType(this->isolate_,
                            TypeError::Details[TypeError::ID],
                            "* wants integer, not '%s'",
                            nullptr,
                            obj);

        this->failed_ = true;

        return false;
    }

    if (opt < 0) {
        if (!prec)
            this->fmt_.flags |= FormatFlags::LJUST;

        opt = -opt;
    }

    if (!prec)
        this->fmt_.width = (int) opt;
    else
        this->fmt_.prec = (int) opt;

    this->fmt_.nspec++;

    return true;
}

int StringFormatter::FormatChar() {
    OObject *obj = this->NextArg();
    if (obj == nullptr)
        return -1;


    MSSize written;
    if (O_IS_OBJECT(obj) && O_IS_TYPE(obj, InstanceType::STRING)) {
        const auto *str = (const ORString *) obj;

        if (str->cp_length > 1) {
            ErrorSet(this->isolate_,
                     TypeError::Details[TypeError::ID],
                     nullptr,
                     "%%c requires a single character, not a multi-character string");

            this->failed_ = true;

            return -1;
        }

        written = this->Write(str->buffer, str->length, 0);
    } else {
        IntegerUnderlying codepoint = 0;
        if (!NumberExtract(obj, codepoint)) {
            ErrorSetWithObjType(this->isolate_,
                                TypeError::Details[TypeError::ID],
                                "%%c requires an integer or single-character string, not '%s'",
                                nullptr,
                                obj);

            this->failed_ = true;

            return -1;
        }

        unsigned char sequence[4]{};
        const int slen = StringIntToUTF8((unsigned int) codepoint, sequence);
        if (slen == 0) {
            ErrorSet(this->isolate_,
                     ValueError::Details[ValueError::ID],
                     nullptr,
                     "%%c arg 0x%llx is not a valid Unicode codepoint",
                     (unsigned long long) (UIntegerUnderlying) codepoint);

            this->failed_ = true;

            return -1;
        }

        written = this->Write(sequence, (MSize) slen, 0);
    }

    if (written < 0)
        return -1;

    // Return right-padding for LJUST
    if (this->fmt_.width > 0 && written < (MSSize) this->fmt_.width)
        return (int) ((MSSize) this->fmt_.width - written);

    return 0;
}

int StringFormatter::FormatDecimal(unsigned char op) {
    OObject *obj = this->NextArg();
    if (obj == nullptr)
        return -1;

    if (!O_IS_OBJECT(obj) || !O_IS_TYPE(obj, InstanceType::DECIMAL)) {
        char type_name[64];

        GetTypeName(this->isolate_, obj, type_name, sizeof(type_name));

        ErrorSet(this->isolate_,
                 TypeError::Details[TypeError::ID],
                 nullptr,
                 "%%%c requires a decimal value, not '%s'", (char) op, type_name);

        this->failed_ = true;

        return -1;
    }

    const auto *decimal = (const Decimal *) obj;

    // Build snprintf format string: %[flags][width][.prec]L<op>
    // Example: "%-10.4Lf"
    char fmt[32]{};
    int fi = 0;

    fmt[fi++] = '%';

    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::LJUST))
        fmt[fi++] = '-';
    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::SIGN))
        fmt[fi++] = '+';
    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::BLANK))
        fmt[fi++] = ' ';
    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::ZERO))
        fmt[fi++] = '0';
    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::ALT))
        fmt[fi++] = '#';

    if (this->fmt_.width > 0)
        fi += std::snprintf(fmt + fi, sizeof(fmt) - (size_t) fi, "%d", this->fmt_.width);

    if (this->fmt_.prec > -1)
        fi += std::snprintf(fmt + fi, sizeof(fmt) - (size_t) fi, ".%d", this->fmt_.prec);

    fmt[fi++] = 'L'; // long double modifier
    fmt[fi++] = (char) op;
    fmt[fi] = '\0';

    // Measure the output length
    const int len = std::snprintf(nullptr, 0, fmt, decimal->value);
    if (len < 0) {
        ErrorSet(this->isolate_,
                 RuntimeError::Details[RuntimeError::ID],
                 nullptr,
                 "snprintf failed while formatting decimal value");

        this->failed_ = true;

        return -1;
    }

    // Allocate space (+1 for '\0') and write
    if (!this->BufferResize((MSize) len + 1))
        return -1;

    std::snprintf((char *) this->output_.cursor, (size_t) len + 1, fmt, decimal->value);
    this->output_.cursor += len;

    return 0; // snprintf already applied width/ljust; no extra padding needed
}

int StringFormatter::FormatInteger(int base, bool unsign, bool upper) {
    OObject *obj = this->NextArg();
    if (obj == nullptr)
        return -1;

    IntegerUnderlying sint_val = 0;
    UIntegerUnderlying uint_val = 0;

    if (unsign && O_IS_OBJECT(obj) && O_IS_TYPE(obj, InstanceType::NUMBER)) {
        // Unsigned format: read the unsigned field directly to preserve
        // the bit pattern for values > INT64_MAX.
        uint_val = ((const Number *) obj)->uint;
    } else {
        if (!NumberExtract(obj, sint_val)) {
            char type_name[64];
            GetTypeName(this->isolate_, obj, type_name, sizeof(type_name));

            ErrorSet(this->isolate_,
                     TypeError::Details[TypeError::ID],
                     nullptr,
                     "%%%c requires an integer, not '%s'",
                     *this->fmt_.cursor,
                     type_name);

            this->failed_ = true;

            return -1;
        }

        uint_val = (UIntegerUnderlying) sint_val;
    }

    // Pre-compute the buffer size needed
    int prec = 0;
    int bufsz = unsign ? DigitCountU(uint_val, base) : DigitCountS(sint_val, base);

    if (this->fmt_.prec > bufsz)
        bufsz = this->fmt_.prec;

    // Sign character
    const bool is_neg = (!unsign && sint_val < 0);
    if (is_neg || ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::SIGN))
        bufsz++;

    // Alternate prefix (0b / 0o / 0x)
    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::ALT))
        bufsz += 2;

    // Blank space before positive numbers
    if (ENUMBITMASK_ISTRUE(this->fmt_.flags, FormatFlags::BLANK))
        bufsz++;

    // Width padding (only for right-justify; left-justify handled after)
    if (this->fmt_.width > bufsz)
        bufsz += this->fmt_.width - bufsz;

    if (!this->BufferResize((MSize) bufsz))
        return -1;

    if (this->fmt_.prec > -1)
        prec = this->fmt_.prec;

    int diff;
    if (!unsign)
        diff = WriteNumber(this->output_.cursor,
                           sint_val, base, prec,
                           this->fmt_.width, upper, this->fmt_.flags);
    else
        diff = WriteNumber(this->output_.cursor,
                           uint_val, base, prec,
                           this->fmt_.width, upper, this->fmt_.flags);

    this->output_.cursor += diff;

    // Return right-padding count for LJUST
    return bufsz - diff;
}

// static
int StringFormatter::FormatNumber(unsigned char *buf, int index, int base,
                                  int width, bool upper, bool neg, FormatFlags flags) {
    // Zero-padding (between sign/prefix and digits)
    if (ENUMBITMASK_ISTRUE(flags, FormatFlags::ZERO)) {
        width = width > index ? width - index : 0;
        while (width--)
            buf[index++] = '0';
    }

    // Alternate prefix: 0b / 0B, 0o, 0x / 0X
    if (ENUMBITMASK_ISTRUE(flags, FormatFlags::ALT)) {
        if (base == 2)
            buf[index++] = upper ? 'B' : 'b';
        else if (base == 8)
            buf[index++] = 'o';
        else if (base == 16)
            buf[index++] = upper ? 'X' : 'x';

        buf[index++] = '0';
    }

    // Sign
    if (!neg) {
        if (ENUMBITMASK_ISTRUE(flags, FormatFlags::SIGN))
            buf[index++] = '+';
    } else
        buf[index++] = '-';

    // Blank space for positive numbers
    if (ENUMBITMASK_ISTRUE(flags, FormatFlags::BLANK))
        buf[index++] = ' ';

    // Space-padding for right-justify (between start of field and sign/prefix)
    if (ENUMBITMASK_ISFALSE(flags, FormatFlags::LJUST)) {
        width = width > index ? width - index : 0;
        while (width--)
            buf[index++] = ' ';
    }

    // Reverse the buffer in-place (digits were written least-significant first)
    unsigned char *end = buf + index;
    while (buf < end) {
        const unsigned char tmp = *buf;
        *buf++ = *(end - 1);
        *--end = tmp;
    }

    return index;
}

// =============================================================================
// Public methods
// =============================================================================

unsigned char *StringFormatter::Format(MSize *out_len, MSize *out_cap) {
    if (this->output_.buffer != nullptr) {
        *out_len = (MSize) (this->output_.cursor - this->output_.buffer);
        *out_cap = (MSize) (this->output_.end - this->output_.buffer);

        return this->output_.buffer;
    }

    // Determine how many arguments we expect
    this->fmt_.args_length = 1;
    if (O_IS_OBJECT(this->fmt_.args) && O_IS_TYPE(this->fmt_.args, InstanceType::TUPLE))
        this->fmt_.args_length = ((const Tuple *) this->fmt_.args)->length;

    *out_len = 0;
    *out_cap = 0;

    while (this->NextSpecifier()) {
        if (!this->ParseOption())
            return nullptr;

        if (!this->FormatSpecifier())
            return nullptr;
    }

    if (this->failed_)
        return nullptr;

    // All arguments must have been consumed
    if (this->fmt_.nspec < (int) this->fmt_.args_length) {
        ErrorSet(this->isolate_,
                 ValueError::Details[ValueError::ID],
                 nullptr,
                 "not all arguments converted during string formatting");

        this->failed_ = true;

        return nullptr;
    }

    *out_len = (MSize) (this->output_.cursor - this->output_.buffer);
    *out_cap = (MSize) (this->output_.end - this->output_.buffer);

    // Null-terminate (space was reserved by BufferResize)
    if (this->output_.cursor != nullptr)
        *this->output_.cursor = '\0';

    return this->output_.buffer;
}

HORString StringFormatter::FormatToString() {
    MSize len;
    MSize cap;

    auto *buf = this->Format(&len, &cap);
    if (buf == nullptr || this->failed_)
        return {};

    auto result = ORStringNewHoldBuffer(this->isolate_, buf, len);
    if (!result)
        return {};

    // Transfer ownership of the buffer (allocated via IsolateAllocator) to the
    // new ORString.
    this->ReleaseOwnership();

    return result;
}

void StringFormatter::ReleaseOwnership() {
    this->output_.buffer = nullptr;
    this->output_.cursor = nullptr;
    this->output_.end = nullptr;
}

StringFormatter::~StringFormatter() {
    this->allocator_.free(this->output_.buffer);
}
