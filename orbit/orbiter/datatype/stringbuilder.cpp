// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include <orbit/orbiter/datatype/stringbuilder.h>

using namespace orbiter::datatype;

MSize StringBuilder::GetEscapedLength(const unsigned char *buffer, MSize length, bool unicode) {
    MSize str_len = 0;

    for (MSize i = 0; i < length; i++) {
        switch (buffer[i]) {
            case '"':
            case '\\':
            case '\t':
            case '\n':
            case '\r':
                str_len += 2; // \C
                break;
            default:
                if (!unicode && (buffer[i] < ' ' || buffer[i] >= 0x7F)) {
                    str_len += 4;
                    break;
                }
                str_len++;
        }
    }

    return str_len;
}

MSize StringBuilder::GetUnescapedLength(const unsigned char *buffer, MSize length) {
    MSize str_len = 0;
    MSize i = 0;

    while (i < length) {
        if (buffer[i] == '\\' && i + 1 < length) {
            switch (buffer[++i]) {
                case 'a':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                case 'v':
                case 'x':
                case '\\':
                    break;
                case 'u':
                    i += 5;
                    str_len += 2;
                    continue;
                case 'U':
                    i += 9;
                    str_len += 4;
                    continue;
                default:
                    str_len += 2;
                    i++;
                    continue;
            }
        }

        str_len++;
        i++;
    }

    return str_len;
}

int StringBuilder::HexToByte(const unsigned char *buffer, MSize length) {
    int byte = 0;
    int curr;

    if (length < 2) {
        snprintf(this->e_msg_, kERR_MSG_MAX_LENGTH, "can't decode byte, hex escape must be: \\xhh");
        return -1;
    }

    for (int i = 1; i >= 0; i--) {
        curr = *buffer++;

        if (!isxdigit(curr)) {
            snprintf(this->e_msg_, kERR_MSG_MAX_LENGTH, "'%c' invalid hex digit, can't decode hex escape",
                     curr);
            return -1;
        }

        byte |= (isdigit(curr) ? ((char) curr) - '0' : 10 + (tolower(curr) - 'a')) << (unsigned char) (i * 4);
    }

    return byte;
}

int StringBuilder::ProcessUnicodeEscape(unsigned char *wb, const unsigned char *buffer, MSize length, bool extended) {
    unsigned char sequence[] = {0, 0, 0, 0};
    int width = 2;
    int byte;
    int ulen;

    if (extended)
        width = 4;

    for (int i = 0; i < width; i++) {
        if ((byte = this->HexToByte(buffer + (i * 2), length - i)) < 0) {
            snprintf(this->e_msg_, kERR_MSG_MAX_LENGTH,
                     "can't decode bytes in unicode sequence, escape format must be: %s",
                     extended ? "\\Uhhhhhhhh" : "\\uhhhh");

            return false;
        }
        sequence[(width - 1) - i] = (unsigned char) byte;
    }

    if ((ulen = StringIntToUTF8(*((unsigned int *) sequence), wb)) == 0) {
        snprintf(this->e_msg_, kERR_MSG_MAX_LENGTH, "can't decode bytes in unicode sequence");

        return false;
    }

    if ((StringKind) (ulen - 1) > this->kind_)
        this->kind_ = (StringKind) (ulen - 1);

    return ulen;
}

StringBuilder::~StringBuilder() {
    this->allocator_.free(this->buffer_);
}

bool StringBuilder::BufferResize(MSize sz) {
    MSize cap = this->cap_;
    unsigned char *tmp;

    if (this->e_msg_[0] != '\0')
        return false;

    if (cap > 0)
        cap--;

    if (sz == 0 || this->len_ + sz < cap)
        return true;

    if (this->buffer_ == nullptr)
        sz += 1;

    if ((tmp = this->allocator_.realloc(this->buffer_, this->cap_ + sz)) == nullptr)
        return false;

    this->buffer_ = tmp;
    this->cap_ += sz;
    return true;
}

bool StringBuilder::ParseEscaped(const unsigned char *buffer, MSize length) {
    StringKind kind = StringKind::ASCII;
    unsigned char *wbuf;
    MSize idx = 0;
    MSize uidx = 0;

    if (buffer == nullptr || length == 0)
        return true;

    MSize wlen = GetUnescapedLength(buffer, length);

    if (!this->BufferResize(wlen))
        return false;

    wbuf = this->buffer_ + this->len_;

    while (idx < length) {
        if (idx == uidx && buffer[idx] == '\\') {
            idx++;
            uidx++;

            if (idx >= length) {
                *wbuf++ = '\\';
                break;
            }

            switch (buffer[idx]) {
                case 'a':
                    *wbuf++ = 0x07;
                    break;
                case 'b':
                    *wbuf++ = 0x08;
                    break;
                case 'f':
                    *wbuf++ = 0x0C;
                    break;
                case 'n':
                    *wbuf++ = 0x0A;
                    break;
                case 'r':
                    *wbuf++ = 0x0D;
                    break;
                case 't':
                    *wbuf++ = 0x09;
                    break;
                case 'v':
                    *wbuf++ = 0x0B;
                    break;
                case 'x':
                    idx++;
                    *wbuf++ = (unsigned char) this->HexToByte(buffer + idx, length - idx);
                    idx += 1;
                    uidx += 2;
                    break;
                case '\\':
                    *wbuf++ = '\\';
                    break;
                case 'u':
                    idx++;
                    wbuf += this->ProcessUnicodeEscape(wbuf, buffer + idx, length - idx, false);
                    idx += 3;
                    uidx += 4;
                    break;
                case 'U':
                    idx++;
                    wbuf += this->ProcessUnicodeEscape(wbuf, buffer + idx, length - idx, true);
                    idx += 7;
                    uidx += 8;
                    break;
                default:
                    *wbuf++ = '\\';
                    *wbuf++ = buffer[idx];
            }

            if (this->e_msg_[0] != '\0')
                return false;

            uidx++;
        } else {
            *wbuf++ = buffer[idx];

            if (!CheckUnicodeCharSequence(&kind, &uidx, this->e_msg_, kERR_MSG_MAX_LENGTH, buffer[idx], idx))
                return false;
        }

        if (++idx == uidx)
            this->cp_len_++;

        if (kind > this->kind_)
            this->kind_ = kind;
    }

    this->len_ += wbuf - (this->buffer_ + this->len_);
    return true;
}

bool StringBuilder::Write(const unsigned char *buffer, MSize length, MSize overalloc) {
    StringKind kind = StringKind::ASCII;
    MSize idx = 0;
    MSize uidx = 0;
    unsigned char *wbuf;

    if (buffer == nullptr || length == 0)
        return true;

    if (!this->BufferResize(length + overalloc))
        return false;

    wbuf = this->buffer_ + this->len_;

    while (idx < length) {
        wbuf[idx] = buffer[idx];

        if (!CheckUnicodeCharSequence(&kind, &uidx, this->e_msg_, kERR_MSG_MAX_LENGTH, buffer[idx], idx))
            return false;

        if (++idx == uidx)
            this->cp_len_++;

        if (kind > this->kind_)
            this->kind_ = kind;
    }

    this->len_ += idx;
    return true;
}

bool StringBuilder::WriteEscaped(const unsigned char *buffer, MSize length, MSize overalloc, bool unicode) {
    static const unsigned char hex[] = "0123456789abcdef";
    const unsigned char *start;
    unsigned char *buf;

    MSize wlen = StringBuilder::GetEscapedLength(buffer, length, unicode);

    if (!this->BufferResize(wlen + overalloc))
        return false;

    if (length == 0)
        return true;

    start = this->buffer_ + this->len_;
    buf = this->buffer_ + this->len_;

    for (MSize i = 0; i < length; i++) {
        switch (buffer[i]) {
            case '"':
                *buf++ = '\\';
                *buf++ = '"';
                break;
            case '\\':
                *buf++ = '\\';
                *buf++ = '\\';
                break;
            case '\t':
                *buf++ = '\\';
                *buf++ = 't';
                break;
            case '\n':
                *buf++ = '\\';
                *buf++ = 'n';
                break;
            case '\r':
                *buf++ = '\\';
                *buf++ = 'r';
                break;
            default:
                if (!unicode && (buffer[i] < ' ' || buffer[i] >= 0x7F)) {
                    *buf++ = '\\';
                    *buf++ = 'x';
                    *buf++ = hex[(buffer[i] & 0xF0) >> 4];
                    *buf++ = hex[buffer[i] & 0x0F];
                    break;
                }

                *buf++ = buffer[i];
        }
    }

    this->len_ += buf - start;
    this->cp_len_ += buf - start;
    return length;
}

bool StringBuilder::WriteHex(const unsigned char *buffer, MSize length) {
    static const unsigned char hex[] = "0123456789abcdef";
    unsigned char *buf;
    MSize wlen = length * 4;

    if (!this->BufferResize(wlen))
        return false;

    if (length == 0)
        return true;

    buf = this->buffer_;

    for (MSize i = 0; i < length; i++) {
        *buf++ = '\\';
        *buf++ = 'x';
        *buf++ = hex[(buffer[i] & 0xF0) >> 4];
        *buf++ = hex[buffer[i] & 0x0F];
    }

    this->len_ += wlen;
    this->cp_len_ += wlen;

    return true;
}

bool StringBuilder::WriteRepeat(char ch, int times) {
    if (!this->BufferResize(times))
        return false;

    for (int i = 0; i < times; i++)
        this->buffer_[this->len_++] = ch;

    this->cp_len_ += times;

    return true;
}

const char *StringBuilder::GetErrorMessage() {
    return this->e_msg_;
}

unsigned char *StringBuilder::BuildString(MSize *cap, MSize *len, MSize *cp_len, StringKind *kind) {
    if (this->e_msg_[0] != '\0')
        return nullptr;

    if (len != nullptr)
        *len = this->len_;

    if (cp_len != nullptr)
        *cp_len = this->cp_len_;

    if (kind != nullptr)
        *kind = this->kind_;

    if (this->buffer_ == nullptr || this->len_ == 0) {
        if (!this->BufferResize(1))
            return nullptr;
    }

    assert(this->len_ < this->cap_);

    this->buffer_[this->len_] = '\0';

    if (cap != nullptr)
        *cap = this->cap_;

    return this->buffer_;
}

// Functions

bool orbiter::datatype::CheckUnicodeCharSequence(StringKind *out_kind, MSize *out_uidx, char *out_error,
                                                 U16 out_error_length, unsigned char chr, MSize index) {
    if (index == *out_uidx) {
        if (chr >> 7u == 0x0)
            *out_uidx += 1;
        else if (chr >> 5u == 0x6) {
            *out_kind = StringKind::UTF8_2;
            *out_uidx += 2;
        } else if (chr >> 4u == 0xE) {
            *out_kind = StringKind::UTF8_3;
            *out_uidx += 3;
        } else if (chr >> 3u == 0x1E) {
            *out_kind = StringKind::UTF8_4;
            *out_uidx += 4;
        } else if (chr >> 6u == 0x2) {
            snprintf(out_error, out_error_length, "can't decode byte 0x%x: invalid start byte", chr);
            return false;
        }
    } else if (chr >> 6u != 0x2) {
        snprintf(out_error, out_error_length, "can't decode byte 0x%x: invalid continuation byte", chr);
        return false;
    }

    return true;
}

int orbiter::datatype::StringIntToUTF8(const unsigned int glyph, unsigned char *buf) {
    if (glyph < 0x80) {
        *buf = glyph >> 0u & 0x7Fu;
        
        return 1;
    } 
    
    if (glyph < 0x0800) {
        *buf++ = glyph >> 6u & 0x1Fu | 0xC0u;
        *buf = 0x80 | (glyph & 0x3F);
        
        return 2;
    }

    // This check rejects UTF-16 surrogate pairs (0xD800-0xDFFF), which are invalid Unicode code points.
    // Surrogate pairs are reserved for UTF-16 encoding and have no meaning in UTF-8 or as standalone
    // Unicode scalar values. Attempting to encode them would produce ill-formed UTF-8 sequences.
    // Returning 0 signals an error to the caller, preventing invalid data from being written to the buffer.
    if (glyph >= 0xD800 && glyph <= 0xDFFF)
        return 0;
    
    if (glyph < 0x010000) {
        *buf++ = glyph >> 12u & 0x0Fu | 0xE0u;
        *buf++ = glyph >> 6u & 0x3Fu | 0x80u;
        *buf = glyph >> 0u & 0x3Fu | 0x80u;
        
        return 3;
    } 
    
    if (glyph < 0x110000) {
        *buf++ = glyph >> 18u & 0x07u | 0xF0u;
        *buf++ = glyph >> 12u & 0x3Fu | 0x80u;
        *buf++ = glyph >> 6u & 0x3Fu | 0x80u;
        *buf = glyph >> 0u & 0x3Fu | 0x80u;
        
        return 4;
    }

    return 0;
}

int orbiter::datatype::StringUTF8ToInt(const unsigned char *buf) {
    if (*buf > 0xF4)
        return -1;

    if ((*buf & 0xF0) == 0xF0)
        return (*buf & 0x07) << 18 | (buf[1] & 0x3F) << 12 | (buf[2] & 0x3F) << 6 | buf[3] & 0x3F;
    
    if ((*buf & 0xE0) == 0xE0)
        return (*buf & 0x0F) << 12 | (buf[1] & 0x3F) << 6 | buf[2] & 0x3F;
    
    if ((*buf & 0xC0) == 0xC0)
        return (*buf & 0x1F) << 6 | buf[1] & 0x3F;

    return *buf;
}
