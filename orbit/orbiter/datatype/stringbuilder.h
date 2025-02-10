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

        void Release() {
            this->buffer_ = nullptr;
        }
    };

    bool CheckUnicodeCharSequence(StringKind *out_kind, MSize *out_uidx, char *out_error,
                                  U16 out_error_length, unsigned char chr, MSize index);

    int StringIntToUTF8(unsigned int glyph, unsigned char *buf);

    int StringUTF8ToInt(const unsigned char *buf);
}

#endif // !ORBIT_ORBITER_DATATYPE_STRINGBUILDER_H_
