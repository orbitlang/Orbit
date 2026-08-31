// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_STRINGBUILDER_H_
#define ORBIT_ORBITER_DATATYPE_STRINGBUILDER_H_

#include <orbit/datatype.h>

#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::datatype {
    constexpr int kERR_MSG_MAX_LENGTH = 80;

    class StringBuilder {
        memory::IsolateAllocator allocator_;

        char e_msg_[kERR_MSG_MAX_LENGTH]{};

        unsigned char *buffer_ = nullptr;

        MSize cap_ = 0;
        MSize len_ = 0;
        MSize cp_len_ = 0;

        StringKind kind_ = StringKind::ASCII;

        static MSize GetEscapedLength(const unsigned char *buffer, MSize length, bool unicode);

        static MSize GetUnescapedLength(const unsigned char *buffer, MSize length);

        int HexToByte(const unsigned char *buffer, MSize length);

        int ProcessUnicodeEscape(unsigned char *wb, const unsigned char *buffer, MSize length, bool extended);

    public:
        explicit StringBuilder(Isolate *isolate) : allocator_(isolate) {
        }

        ~StringBuilder();

        bool BufferResize(MSize sz);

        [[nodiscard]] bool InError() const {
            return this->e_msg_[0] != '\0';
        }

        bool ParseEscaped(const unsigned char *buffer, MSize length);

        bool Write(const unsigned char *buffer, MSize length, MSize overalloc);

        bool Write(const ORString *string, MSize overalloc) {
            return this->Write(string->buffer, string->length, overalloc);
        }

        bool WriteEscaped(const unsigned char *buffer, MSize length, MSize overalloc, bool unicode);

        bool WriteEscaped(const unsigned char *buffer, MSize length, MSize overalloc) {
            return this->WriteEscaped(buffer, length, overalloc, false);
        }

        bool WriteHex(const unsigned char *buffer, MSize length);

        bool WriteRepeat(char ch, int times);

        const char *GetErrorMessage();

        [[nodiscard]] unsigned char *BuildString(MSize *cap, MSize *len, MSize *cp_len, StringKind *kind);

        [[nodiscard]] unsigned char *BuildString() {
            return this->BuildString(nullptr, nullptr, nullptr, nullptr);
        }

        void Release() {
            this->buffer_ = nullptr;
        }
    };

    bool CheckUnicodeCharSequence(StringKind *out_kind, MSize *out_uidx, char *out_error,
                                  U16 out_error_length, unsigned char chr, MSize index);

    /**
     * @brief Check that a buffer holds every byte of the code point its lead byte announces.
     *
     * Reads @p buffer[0] to determine how many bytes the encoded code point occupies
     * (1, 2, 3 or 4) and reports whether @p length covers all of them.
     *
     * This is a bounds check, not a validity check: it makes it safe to call
     * StringUTF8ToInt, which reads up to three continuation bytes past the lead byte
     * without inspecting the buffer size. Continuation-byte values, overlong encodings,
     * and surrogates are not verified, and trailing bytes after the first code point are
     * allowed. Use StringUTF8IsSingleCodePoint when the buffer must hold one code
     * point and nothing else.
     *
     * @param buffer Buffer to inspect. Must contain at least one readable byte.
     * @param length Number of readable bytes in @p buffer.
     * @return true if the announced sequence fits entirely within @p length, false if it
     * is truncated.
     */
    bool StringUTF8HasCompleteSequence(const unsigned char *buffer, MSize length);

    /**
     * @brief Check that a buffer is exactly one well-formed UTF-8 code point.
     *
     * Stricter counterpart of StringUTF8HasCompleteSequence: the lead byte must be a legal
     * start byte and @p length must match the sequence it announces exactly. Rejects
     * bare continuation bytes (0x80-0xBF), the invalid 5/6-byte lead bytes (0xF8-0xFF),
     * truncated sequences, and buffers carrying extra bytes after a complete code point.
     *
     * Only the lead byte and the length are validated. Continuation-byte values, overlong
     * encodings, and surrogate code points are not checked, so callers decoding untrusted
     * input need a full validation pass on top of this.
     *
     * @param buffer Buffer to inspect. Must contain at least one readable byte.
     * @param length Number of readable bytes in @p buffer.
     * @return true if @p buffer is exactly one code point, false otherwise.
     */
    bool StringUTF8IsSingleCodePoint(const unsigned char *buffer, MSize length);

    int StringIntToUTF8(unsigned int glyph, unsigned char *buf);

    int StringUTF8ToInt(const unsigned char *buf);
}

#endif // !ORBIT_ORBITER_DATATYPE_STRINGBUILDER_H_
