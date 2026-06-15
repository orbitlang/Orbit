// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_SCANNER_SCANNER_H_
#define ORBIT_LIFTOFF_SCANNER_SCANNER_H_

#include <cstdio>
#include <cstring>

#include <orbit/orbiter/isolate.h>

#include <orbit/liftoff/scanner/ibuffer.h>
#include <orbit/liftoff/scanner/sbuffer.h>
#include <orbit/liftoff/scanner/token.h>

namespace liftoff::scanner {
    constexpr auto kScannerFileBuffer = 4096; // 4KiB
    constexpr auto kScannerPromptBuffer = 1024; // 1KiB

    enum class ScannerStatus {
        EMPTY_SQUOTE,
        END_OF_FILE,
        INVALID_BINARY_LITERAL,
        INVALID_BSTR,
        INVALID_BYTE_ULONG,
        INVALID_BYTE_USHORT,
        INVALID_HEX_BYTE,
        INVALID_HEX_LITERAL,
        INVALID_LC,
        INVALID_NUMBER_LITERAL,
        INVALID_OCTAL_LITERAL,
        INVALID_OCT_BYTE,
        INVALID_RSTR,
        INVALID_RS_PROLOGUE,
        INVALID_SQUOTE,
        INVALID_STR,
        INVALID_TK,
        INVALID_UCHR,
        INVALID_U_NUM,
        INVALID_NULLBYTE,
        IO_ERROR,
        NOMEM,
        GOOD
    };

    using InteractiveFn = int (*)(const char *prompt, FILE *fd, InputBuffer *ibuf);

    class Scanner {
        orbiter::Isolate *isolate_;

        const char *prompt_ = nullptr;
        const char *next_prompt_ = nullptr;

        FILE *fd_ = nullptr;

        InteractiveFn promptfn_ = nullptr;

        StoreBuffer sbuf_;

        InputBuffer ibuf_;

        Token peeked;

        Position loc{1, 1, 0};

        bool NextTokenInternal(Token *out_token) noexcept;

        bool ParseEscape(int stop, bool ignore_unicode);

        bool ParseHexEscape();

        bool ParseOctEscape(int value);

        bool ParseUnicode(bool extended);

        bool TokenizeAtom(Token *out_token);

        bool TokenizeBinary(Token *out_token);

        bool TokenizeChar(Token *out_token);

        bool TokenizeComment(Token *out_token, bool inline_comment);

        bool TokenizeDecimal(Token *out_token, TokenType type, bool begin_zero);

        bool TokenizeHex(Token *out_token);

        bool TokenizeNumber(Token *out_token);

        bool TokenizeOctal(Token *out_token);

        bool TokenizeString(Token *out_token, bool check_prefix, bool byte_string);

        bool TokenizeWord(Token *out_token);

        int HexToByte();

        int Next() { return this->Peek(true); }

        int Peek(bool advance);

        int Peek() { return this->Peek(false); }

        int UnderflowInteractive();

    public:
        ScannerStatus status = ScannerStatus::GOOD;

        /**
         * @brief Initialize scanner using a string and length.
         *
         * @param isolate Pointer to the Isolate
         * @param str Pointer to the string that contains the source code.
         * @param length Length of the string that contains the source code.
         */
        Scanner(orbiter::Isolate *isolate, const char *str,
                unsigned long length) noexcept: isolate_(isolate), sbuf_(isolate),
                                                ibuf_(isolate, (unsigned char *) str, length) {
        }

        /**
         * @brief Initialize scanner using a string.
         *
         * @param isolate Pointer to the Isolate
         * @param str Pointer to the string that contains the source code.
         */
        explicit Scanner(orbiter::Isolate *isolate, const char *str) noexcept: Scanner(isolate, str, strlen(str)) {
        }

        /**
         * @brief Initialize scanner using a file to read from and prompts to show (interactive mode).
         *
         * @param isolate Pointer to the Isolate
         * @param fd Pointer to FILE.
         * @param ps1 Pointer to Prompt 1.
         * @param ps2 Pointer to Prompt 2.
         * @param buf_size Size of internal working buffer.
         */
        Scanner(orbiter::Isolate *isolate, FILE *fd, const char *ps1, const char *ps2, int buf_size) noexcept;

        /**
         * @brief Initialize scanner using a file to read from and prompts to show (interactive mode).
         *
         * @param isolate Pointer to the Isolate
         * @param fd Pointer to FILE.
         * @param ps1 Pointer to Prompt 1.
         * @param ps2 Pointer to Prompt 2.
         */
        Scanner(orbiter::Isolate *isolate, FILE *fd, const char *ps1, const char *ps2) noexcept: Scanner(
            isolate, fd, ps1, ps2, kScannerFileBuffer) {
        }

        /**
         * @brief Reads the next token from the stream and returns it.
         *
         * @param out_token Pointer to the token to fill.
         * @return True in case of success, false otherwise (you can use GetStatusMessage to know the error).
         */
        bool NextToken(Token *out_token) noexcept;

        /**
         * @brief Peek the next Token.
         *
         * @warning The returned pointer points to an internal structure,
         * DO NOT modify its contents!
         *
         * @param out_token A pointer to a variable which will hold the pointer to the scanner internal token.
         * @return True in case of success, false otherwise (you can use GetStatusMessage to know the error).
         */
        bool PeekToken(const Token **out_token) noexcept;

        /**
         * @brief Get Scanner status message.
         *
         * @return Scanner status message.
         */
        [[nodiscard]] const char *GetStatusMessage() const;
    };
}

#endif // !ORBIT_LIFTOFF_SCANNER_SCANNER_H_
