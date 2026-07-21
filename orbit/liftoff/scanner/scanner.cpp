// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <cctype>
#include <cstdlib>

#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/liftoff/scanner/scanner.h>

using namespace liftoff::scanner;

struct KwToken {
    const char *keyword;
    TokenType type;
};

constexpr KwToken kw2tktype[] = {
    {"bool", TokenType::DT_BOOL},
    {"byte", TokenType::DT_BYTE},

    {"i8", TokenType::DT_I8},
    {"i16", TokenType::DT_I16},
    {"i32", TokenType::DT_I32},
    {"i64", TokenType::DT_I64},
    {"iSize", TokenType::DT_ISIZE},

    {"u8", TokenType::DT_U8},
    {"u16", TokenType::DT_U16},
    {"u32", TokenType::DT_U32},
    {"u64", TokenType::DT_U64},
    {"uSize", TokenType::DT_USIZE},
    {"unit", TokenType::DT_UNIT},
    {"opaque", TokenType::DT_OPAQUE},
    {"ptr", TokenType::DT_PTR},

    {"f32", TokenType::DT_F32},
    {"f64", TokenType::DT_F64},

    {"as", TokenType::KW_AS},
    {"async", TokenType::KW_ASYNC},
    {"assert", TokenType::KW_ASSERT},
    {"await", TokenType::KW_AWAIT},
    {"break", TokenType::KW_BREAK},
    {"catch", TokenType::KW_CATCH},
    {"case", TokenType::KW_CASE},
    {"class", TokenType::KW_CLASS},
    {"cleanup", TokenType::KW_CLEANUP},
    {"const", TokenType::KW_CONST},
    {"continue", TokenType::KW_CONTINUE},
    {"default", TokenType::KW_DEFAULT},
    {"defer", TokenType::KW_DEFER},
    {"elif", TokenType::KW_ELIF},
    {"else", TokenType::KW_ELSE},
    {"fallthrough", TokenType::KW_FALLTHROUGH},
    {"false", TokenType::FALSE},
    {"finally", TokenType::KW_FINALLY},
    {"for", TokenType::KW_FOR},
    {"from", TokenType::KW_FROM},
    {"func", TokenType::KW_FUNC},
    {"if", TokenType::KW_IF},
    {"in", TokenType::KW_IN},
    {"init", TokenType::KW_INIT},
    {"impl", TokenType::KW_IMPL},
    {"import", TokenType::KW_IMPORT},
    {"is", TokenType::KW_IS},
    {"let", TokenType::KW_LET},
    {"loop", TokenType::KW_LOOP},
    {"namespace", TokenType::KW_NAMESPACE},
    {"native", TokenType::KW_NATIVE},
    {"new", TokenType::KW_NEW},
    {"nil", TokenType::NIL},
    {"not", TokenType::KW_NOT},
    {"of", TokenType::KW_OF},
    {"panic", TokenType::KW_PANIC},
    {"pub", TokenType::KW_PUB},
    {"prot", TokenType::KW_PROT},
    {"return", TokenType::KW_RETURN},
    {"self", TokenType::SELF},
    {"spawn", TokenType::KW_SPAWN},
    {"super", TokenType::SUPER},
    {"switch", TokenType::KW_SWITCH},
    {"sync", TokenType::KW_SYNC},
    {"trait", TokenType::KW_TRAIT},
    {"trap", TokenType::KW_TRAP},
    {"try", TokenType::KW_TRY},
    {"true", TokenType::TRUE},
    {"var", TokenType::KW_VAR},
    {"yield", TokenType::KW_YIELD},
    {"weak", TokenType::KW_WEAK},
    {"when", TokenType::KW_WHEN}
};

int DefaultPrompt(const char *prompt, FILE *fd, InputBuffer *ibuf) {
    int length = kScannerPromptBuffer;
    int cur = 0;

    unsigned char *buf = nullptr;
    unsigned char *tmp;

    printf("%s", prompt);

    do {
        length += cur >> 1;

        if ((tmp = (unsigned char *) realloc(buf, length)) == nullptr) {
            cur = -1;
            free(buf);

            return cur;
        }

        buf = tmp;

        if (std::fgets((char *) buf + cur, length - cur, fd) == nullptr) {
            cur = -1;

            if (feof(fd) != 0)
                cur = 0;

            free(buf);
            return cur;
        }

        cur += (int) strlen((char *) buf + cur);
    } while (cur == 0 || *((buf + cur) - 1) != '\n');

    if (!ibuf->AppendInput(buf, cur))
        cur = -1;

    free(buf);
    return cur;
}

inline unsigned char HexDigitToNumber(int chr) {
    return (isdigit(chr)) ? ((char) chr) - '0' : (unsigned char) (10 + (tolower(chr) - 'a'));
}

inline bool IsHexDigit(const int chr) {
    return (chr >= '0' && chr <= '9') || (tolower(chr) >= 'a' && tolower(chr) <= 'f');
}

inline bool IsOctDigit(const int chr) { return chr >= '0' && chr <= '7'; }

bool Scanner::ParseEscape(const int stop, const bool ignore_unicode) {
    int value = this->Next();
    bool ok;

    if (value == stop) {
        if (!this->sbuf_.PutChar((unsigned char) value)) {
            this->status = ScannerStatus::NOMEM;

            return false;
        }

        return true;
    }

    if (!ignore_unicode) {
        if (value == 'u')
            return this->ParseUnicode(false);

        if (value == 'U')
            return this->ParseUnicode(true);
    }

    switch (value) {
        case 'a':
            ok = this->sbuf_.PutChar(0x07);
            break;
        case 'b':
            ok = this->sbuf_.PutChar(0x08);
            break;
        case 'f':
            ok = this->sbuf_.PutChar(0x0C);
            break;
        case 'n':
            ok = this->sbuf_.PutChar(0x0A);
            break;
        case 'r':
            ok = this->sbuf_.PutChar(0x0D);
            break;
        case 't':
            ok = this->sbuf_.PutChar(0x09);
            break;
        case 'v':
            ok = this->sbuf_.PutChar(0x0B);
            break;
        case 'x':
            return this->ParseHexEscape();
        default:
            if (IsOctDigit(value))
                return this->ParseOctEscape(value);

            ok = this->sbuf_.PutChar('\\');
            if (!ok || !this->sbuf_.PutChar((unsigned char) value)) {
                ok = false;
                break;
            }
    }

    if (!ok) {
        this->status = ScannerStatus::NOMEM;

        return false;
    }

    return true;
}

bool Scanner::ParseHexEscape() {
    int byte;

    if ((byte = this->HexToByte()) < 0) {
        this->status = ScannerStatus::INVALID_HEX_BYTE;
        return false;
    }

    if (!this->sbuf_.PutChar((unsigned char) byte)) {
        this->status = ScannerStatus::NOMEM;
        return false;
    }

    return true;
}

bool Scanner::ParseOctEscape(const int value) {
    unsigned int byte = 0;

    if (!IsOctDigit(value))
        return false;

    byte = HexDigitToNumber(value);

    for (int i = 0; i < 2 && IsOctDigit(this->Peek()); i++)
        byte = (byte << 3) | HexDigitToNumber(this->Next());

    if (byte > 0xFF) {
        this->status = ScannerStatus::INVALID_OCT_BYTE;

        return false;
    }

    if (!this->sbuf_.PutChar(byte)) {
        this->status = ScannerStatus::NOMEM;

        return false;
    }

    return true;
}

bool Scanner::ParseUnicode(bool extended) {
    unsigned char sequence[] = {0, 0, 0, 0};
    unsigned char buf[4] = {};
    int width = 2;
    int byte;

    if (extended)
        width = 4;

    for (int i = 0; i < width; i++) {
        if ((byte = this->HexToByte()) < 0) {
            this->status = ScannerStatus::INVALID_BYTE_USHORT;
            if (extended)
                this->status = ScannerStatus::INVALID_BYTE_ULONG;
            return false;
        }
        sequence[(width - 1) - i] = (unsigned char) byte;
    }

    const int len = orbiter::datatype::StringIntToUTF8(*((unsigned int *) sequence), buf);
    if (len == 0) {
        this->status = ScannerStatus::INVALID_UCHR;
        return false;
    }

    if (!this->sbuf_.PutString(buf, len)) {
        this->status = ScannerStatus::NOMEM;
        return false;
    }

    return true;
}

bool Scanner::PeekToken(const Token **out_token) noexcept {
    *out_token = &this->peeked;

    if (this->peeked.type != TokenType::TK_NULL)
        return true;

    if (this->NextToken(&this->peeked))
        return true;

    return false;
}

bool Scanner::TokenizeAtom(Token *out_token) {
    int value = this->Peek();

    while (isalnum(value) || value == '_') {
        if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
            this->status = ScannerStatus::NOMEM;

            return false;
        }

        value = this->Peek();
    }

    if (this->sbuf_.GetLength() == 0) {
        this->status = ScannerStatus::INVALID_TK;

        return false;
    }

    out_token->type = TokenType::ATOM;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);

    return true;
}

bool Scanner::TokenizeBinary(Token *out_token) {
    int value = this->Peek();

    while (value >= '0' && value <= '1') {
        if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
            this->status = ScannerStatus::NOMEM;
            return false;
        }

        value = this->Peek();
    }

    if (this->sbuf_.GetLength() == 0 || isdigit(value)) {
        this->status = ScannerStatus::INVALID_BINARY_LITERAL;
        return false;
    }

    out_token->type = TokenType::NUMBER_BIN;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);
    return true;
}

bool Scanner::TokenizeChar(Token *out_token) {
    int value = this->Peek();

    if (value == '\'') {
        this->status = ScannerStatus::EMPTY_SQUOTE;
        return false;
    }

    if (value == '\\') {
        this->Next();

        if (this->Peek() != '\\') {
            if (!this->ParseEscape('\'', false))
                return false;
        } else {
            if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
                this->status = ScannerStatus::NOMEM;
                return false;
            }
        }
    } else {
        if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
            this->status = ScannerStatus::NOMEM;
            return false;
        }
    }

    if (this->Next() != '\'') {
        this->status = ScannerStatus::INVALID_SQUOTE;
        return false;
    }

    out_token->type = TokenType::NUMBER_CHR;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);
    return true;
}

bool Scanner::TokenizeComment(Token *out_token, bool inline_comment) {
    auto type = TokenType::COMMENT;

    if (inline_comment)
        type = TokenType::COMMENT_INLINE;

    // Skip newline/whitespace at comment start
    for (int skip = this->Peek();
         isblank(skip) || (!inline_comment && skip == '\n');
         this->Next(), skip = this->Peek());

    int peek = this->Peek();
    while (peek > 0 && (peek != '\n' || !inline_comment)) {
        peek = this->Next();

        if (!inline_comment && peek == '*' && this->Peek() == '/')
            break;

        if (!this->sbuf_.PutChar((unsigned char) peek)) {
            this->status = ScannerStatus::NOMEM;
            return false;
        }

        peek = this->Peek();
    }

    // EOF terminates an inline comment normally; for a block comment it means
    // the closing '*/' was never found (unterminated).
    if (peek <= 0 && !inline_comment)
        return false;

    out_token->type = type;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);

    if (!inline_comment)
        this->Next();

    return true;
}

bool Scanner::TokenizeDecimal(Token *out_token, TokenType type, const bool begin_zero) {
    if (begin_zero && !this->sbuf_.PutChar('0')) {
        this->status = ScannerStatus::NOMEM;

        return false;
    }

    if (type == TokenType::DECIMAL && !this->sbuf_.PutChar('.')) {
        this->status = ScannerStatus::NOMEM;

        return false;
    }

    while (isdigit(this->Peek())) {
        if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
            this->status = ScannerStatus::NOMEM;

            return false;
        }
    }

    // Look for a fractional part.
    if (this->Peek() == '.' && type != TokenType::DECIMAL) {
        if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
            this->status = ScannerStatus::NOMEM;

            return false;
        }

        while (isdigit(this->Peek())) {
            if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
                this->status = ScannerStatus::NOMEM;

                return false;
            }
        }

        type = TokenType::DECIMAL;
    }

    if (isalpha(this->Peek()) && this->Peek() != 'u' && this->Peek() != 'U') {
        this->status = ScannerStatus::INVALID_NUMBER_LITERAL;

        return false;
    }

    out_token->type = type;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);

    return true;
}

bool Scanner::TokenizeHex(Token *out_token) {
    int value = this->Peek();

    while (IsHexDigit(value)) {
        if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
            this->status = ScannerStatus::NOMEM;
            return false;
        }

        value = this->Peek();
    }

    if (this->sbuf_.GetLength() == 0 || (isalpha(value) && value != 'u' && value != 'U')) {
        this->status = ScannerStatus::INVALID_HEX_LITERAL;
        return false;
    }

    out_token->type = TokenType::NUMBER_HEX;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);
    return true;
}

bool Scanner::TokenizeNumber(Token *out_token) {
    bool ok;

    if (this->Peek() == '0') {
        this->Next();

        switch (tolower(this->Peek())) {
            case 'b':
                this->Next();
                ok = this->TokenizeBinary(out_token);
                break;
            case 'o':
                this->Next();
                ok = this->TokenizeOctal(out_token);
                break;
            case 'x':
                this->Next();
                ok = this->TokenizeHex(out_token);
                break;
            default:
                ok = this->TokenizeDecimal(out_token, TokenType::NUMBER, true);
        }
    } else
        ok = this->TokenizeDecimal(out_token, TokenType::NUMBER, false);

    if (ok && (this->Peek() == 'u' || this->Peek() == 'U')) {
        this->Next();

        switch (out_token->type) {
            case TokenType::NUMBER_BIN:
                out_token->type = TokenType::U_NUMBER_BIN;
                break;
            case TokenType::NUMBER_OCT:
                out_token->type = TokenType::U_NUMBER_OCT;
                break;
            case TokenType::NUMBER_HEX:
                out_token->type = TokenType::U_NUMBER_HEX;
                break;
            case TokenType::NUMBER:
                out_token->type = TokenType::U_NUMBER;
                break;
            default:
                this->status = ScannerStatus::INVALID_U_NUM;
                return false;
        }
    }

    return ok;
}

bool Scanner::TokenizeOctal(Token *out_token) {
    int value = this->Peek();

    while (IsOctDigit(value)) {
        if (!this->sbuf_.PutChar((unsigned char) this->Next())) {
            this->status = ScannerStatus::NOMEM;
            return false;
        }

        value = this->Peek();
    }

    if (this->sbuf_.GetLength() == 0 || isdigit(value)) {
        this->status = ScannerStatus::INVALID_OCTAL_LITERAL;
        return false;
    }

    out_token->type = TokenType::NUMBER_OCT;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);
    return true;
}

bool Scanner::TokenizeString(Token *out_token, const bool check_prefix, const bool byte_string) {
    auto type = TokenType::STRING;
    auto hashes = 0;
    int value;

    if (byte_string)
        type = TokenType::BYTE_STRING;

    // Count beginning hashes
    if (check_prefix)
        for (; this->Peek() == '#'; this->Next(), hashes++);

    if (check_prefix && this->Next() != '"') {
        this->status = ScannerStatus::INVALID_RS_PROLOGUE;

        return false;
    }

    while ((value = this->Next()) > 0) {
        if (value == '"') {
            int count = 0;

            for (; this->Peek() == '#'; this->Next(), count++);

            if (count == hashes) {
                out_token->type = type;
                out_token->loc.end = this->loc;
                out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);

                return true;
            }

            if (!this->sbuf_.PutChar('"')) {
                this->status = ScannerStatus::NOMEM;

                return false;
            }

            if (!this->sbuf_.PutCharRepeat('#', count)) {
                this->status = ScannerStatus::NOMEM;

                return false;
            }

            continue;
        }

        if (value == '\n' && hashes == 0) {
            this->status = ScannerStatus::INVALID_STR;
            return false;
        }

        // Byte string accept byte in range (0x00 - 0x7F)
        if (byte_string && value > 0x7F) {
            this->status = ScannerStatus::INVALID_BSTR;

            return false;
        }

        // Escapes are processed unless the string is "raw". A string is raw
        // when it carries the `r` prefix (check_prefix && !byte_string) OR when
        // it is hash-delimited (hashes > 0) — the latter makes even byte
        // strings raw, e.g. b##"..."##. So: normal "..." and plain byte b"..."
        // interpret '\\'; r"...", r#"..."#, and b#"..."# keep it verbatim.
        if ((!check_prefix || (byte_string && hashes == 0)) && value == '\\') {
            if (this->Peek() != '\\') {
                if (!this->ParseEscape('"', byte_string))
                    return false;

                continue;
            }

            this->Next();
        }

        if (!this->sbuf_.PutChar((unsigned char) value)) {
            this->status = ScannerStatus::NOMEM;
            return false;
        }
    }

    if (value <= 0) {
        this->status = ScannerStatus::INVALID_STR;
        return false;
    }

    out_token->type = type;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);

    return true;
}

bool Scanner::TokenizeWord(Token *out_token) {
    int value = this->Next();

    if (value == 'b' && (this->Peek() == '#' || this->Peek() == '"'))
        return this->TokenizeString(out_token, true, true);

    if (value == 'r' && (this->Peek() == '#' || this->Peek() == '"'))
        return this->TokenizeString(out_token, true, false);

    bool next = false;
    while (isalnum(value) || value == '_') {
        if (!this->sbuf_.PutChar((unsigned char) value)) {
            this->status = ScannerStatus::NOMEM;
            return false;
        }

        if (next)
            this->Next();

        value = this->Peek();
        next = true;
    }

    out_token->type = TokenType::IDENTIFIER;
    out_token->loc.end = this->loc;
    out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);

    if (out_token->length == 1 && *out_token->buffer == '_')
        out_token->type = TokenType::BLANK;

    // keywords are longer than one letter
    if (out_token->length > 1) {
        for (KwToken kt: kw2tktype) {
            auto delta = out_token->length;
            if (strlen(kt.keyword) == delta &&
                orbiter::memory::MemoryCompare(kt.keyword, out_token->buffer, delta) == 0) {
                out_token->type = kt.type;
                break;
            }
        }
    }

    return true;
}

bool Scanner::NextTokenInternal(Token *out_token) noexcept {
    int value;

    // Reset error status
    this->status = ScannerStatus::GOOD;

    // Reset store buffer
    this->sbuf_.Clear();

    // Free the previous buffer (if any) with the isolate it was allocated with.
    // Note: the token's isolate cannot differ from this scanner's isolate, but
    // for correctness we use the one stored in the token.
    if (out_token->buffer != nullptr) {
        const orbiter::memory::IsolateAllocator allocator(out_token->isolate);
        allocator.free(out_token->buffer);

        out_token->buffer = nullptr;
        out_token->length = 0;
    }

    out_token->isolate = this->isolate_;

    if (this->peeked.type != TokenType::TK_NULL) {
        *out_token = std::move(this->peeked);

        this->peeked.type = TokenType::TK_NULL;

        return true;
    }

    while ((value = this->Peek()) > 0) {
#define RETURN_TK(tk_type)          \
    do {                            \
    out_token->loc.end = this->loc; \
    out_token->type = (tk_type);    \
    return true; } while(false)

#define CHECK_AGAIN(chr, tk_type)   \
    if(this->Peek() == (chr)) {     \
        this->Next();               \
        RETURN_TK(tk_type); }

        out_token->loc.start = this->loc;

        // Skip spaces
        if (isblank(value)) {
            while (isblank(this->Peek()))
                this->Next();

            continue;
        }

        if (isalpha(value) || value == '_')
            return this->TokenizeWord(out_token);

        if (isdigit(value))
            return this->TokenizeNumber(out_token);

        this->Next();

        switch (value) {
            case '\r':
                // Only \r\n is a valid line ending; a lone '\r' is not.
                if (this->Peek() != '\n') {
                    this->status = ScannerStatus::INVALID_TK;

                    return false;
                }

                this->Next();

                [[fallthrough]];
            case '\n':
                // Collapse a run of consecutive newlines (\n or \r\n) into one EOL.
                for (;;) {
                    const int nl = this->Peek();

                    if (nl == '\n')
                        this->Next();
                    else if (nl == '\r') {
                        this->Next();

                        if (this->Peek() != '\n') {
                            this->status = ScannerStatus::INVALID_TK;

                            return false;
                        }

                        this->Next();
                    } else
                        break;
                }

                out_token->loc.end = this->loc;
                out_token->type = TokenType::END_OF_LINE;

                return true;
            case '!':
                if (this->Peek() == '=') {
                    this->Next();
                    CHECK_AGAIN('=', TokenType::NOT_EQUAL_STRICT)
                    RETURN_TK(TokenType::NOT_EQUAL);
                }
                RETURN_TK(TokenType::EXCLAMATION);
            case '"':
                return this->TokenizeString(out_token, false, false);
            case '#':
                return this->TokenizeComment(out_token, true);
            case '%':
                RETURN_TK(TokenType::PERCENT);
            case '&':
                CHECK_AGAIN('&', TokenType::AND)
                RETURN_TK(TokenType::AMPERSAND);
            case '\'':
                return this->TokenizeChar(out_token);
            case '(':
                RETURN_TK(TokenType::LEFT_ROUND);
            case ')':
                RETURN_TK(TokenType::RIGHT_ROUND);
            case '*':
                CHECK_AGAIN('=', TokenType::ASSIGN_MUL)
                RETURN_TK(TokenType::ASTERISK);
            case '+':
                CHECK_AGAIN('=', TokenType::ASSIGN_ADD)
                CHECK_AGAIN('+', TokenType::PLUS_PLUS)
                RETURN_TK(TokenType::PLUS);
            case ',':
                RETURN_TK(TokenType::COMMA);
            case '-':
                CHECK_AGAIN('=', TokenType::ASSIGN_SUB)
                CHECK_AGAIN('-', TokenType::MINUS_MINUS)
                CHECK_AGAIN('>', TokenType::ARROW_RIGHT)
                RETURN_TK(TokenType::MINUS);
            case '.':
                if (this->Peek() == '.') {
                    this->Next();
                    CHECK_AGAIN('.', TokenType::ELLIPSIS)
                    this->status = ScannerStatus::INVALID_TK;
                    return false;
                }

                if (isdigit(this->Peek()))
                    return this->TokenizeDecimal(out_token, TokenType::DECIMAL, false);

                RETURN_TK(TokenType::DOT);
            case '/':
                CHECK_AGAIN('/', TokenType::SLASH_SLASH)
                CHECK_AGAIN('=', TokenType::ASSIGN_SLASH)
                if (this->Peek() == '*') {
                    this->Next();

                    if (this->Peek() == '*') {
                        this->Next();

                        // "/**/" is an empty block comment, not a doc comment:
                        // the second '*' was actually the start of the closing
                        // '*/'. Emit a plain (empty) comment.
                        if (this->Peek() == '/') {
                            this->Next();

                            out_token->loc.end = this->loc;
                            out_token->type = TokenType::COMMENT;
                            out_token->length = this->sbuf_.GetBuffer(&out_token->buffer);

                            return true;
                        }

                        if (this->TokenizeComment(out_token, false)) {
                            out_token->type = TokenType::COMMENT_DOC;

                            return true;
                        }

                        return false;
                    }

                    if (this->Peek() == '!') {
                        this->Next();

                        if (this->TokenizeComment(out_token, false)) {
                            out_token->type = TokenType::COMMENT_MODULE;

                            return true;
                        }

                        return false;
                    }

                    return this->TokenizeComment(out_token, false);
                }
                RETURN_TK(TokenType::SLASH);
            case ':':
                CHECK_AGAIN('=', TokenType::WALRUS)
                RETURN_TK(TokenType::COLON);
            case ';':
                RETURN_TK(TokenType::SEMICOLON);
            case '<':
                CHECK_AGAIN('=', TokenType::LESS_EQ)
                CHECK_AGAIN('<', TokenType::SHL)
                CHECK_AGAIN('-', TokenType::ARROW_LEFT)
                RETURN_TK(TokenType::LESS);
            case '=':
                if (this->Peek() == '=') {
                    this->Next();
                    CHECK_AGAIN('=', TokenType::EQUAL_STRICT)
                    RETURN_TK(TokenType::EQUAL_EQUAL);
                }
                RETURN_TK(TokenType::EQUAL);
            case '>':
                CHECK_AGAIN('=', TokenType::GREATER_EQ)
                CHECK_AGAIN('>', TokenType::SHR)
                RETURN_TK(TokenType::GREATER);
            case '?':
                CHECK_AGAIN('?', TokenType::NULL_COALESCING)
                CHECK_AGAIN(':', TokenType::ELVIS)
                CHECK_AGAIN('.', TokenType::QUESTION_DOT)
                RETURN_TK(TokenType::QUESTION);
            case '@':
                CHECK_AGAIN('[', TokenType::DECORATOR);
                return this->TokenizeAtom(out_token);
            case '[':
                RETURN_TK(TokenType::LEFT_SQUARE);
            case '\\': {
                // Line continuation: accept '\'+'\n' and '\'+'\r\n'.
                auto c = this->Next();
                if (c == '\r')
                    c = this->Next();

                if (c != '\n') {
                    this->status = ScannerStatus::INVALID_LC;

                    return false;
                }

                continue;
            }
            case ']':
                RETURN_TK(TokenType::RIGHT_SQUARE);
            case '^':
                RETURN_TK(TokenType::CARET);
            case '{':
                RETURN_TK(TokenType::LEFT_BRACES);
            case '|':
                CHECK_AGAIN('|', TokenType::OR)
                CHECK_AGAIN('>', TokenType::PIPELINE)
                RETURN_TK(TokenType::PIPE);
            case '}':
                RETURN_TK(TokenType::RIGHT_BRACES);
            case '~':
                RETURN_TK(TokenType::TILDE);
            default:
                this->status = ScannerStatus::INVALID_TK;
                return false;
        }
#undef RETURN_TK
#undef CHECK_AGAIN
    }

    if (this->status != ScannerStatus::END_OF_FILE)
        return false;

    out_token->loc.start = this->loc;
    out_token->loc.end = this->loc;
    out_token->type = TokenType::END_OF_FILE;

    return true;
}

bool Scanner::NextToken(Token *out_token) noexcept {
    // On a successful tokenization the status must read GOOD: a lookahead Peek
    // past the last character sets END_OF_FILE as a side effect even when a
    // valid token (including the END_OF_FILE token itself) is produced.
    if (!this->NextTokenInternal(out_token))
        return false;

    this->status = ScannerStatus::GOOD;

    return true;
}

int Scanner::HexToByte() {
    int byte = 0;

    for (int i = 1; i >= 0; i--) {
        const int curr = this->Next();

        if (!IsHexDigit(curr))
            return -1;

        byte |= HexDigitToNumber(curr) << (unsigned char) (i * 4);
    }

    return byte;
}

int Scanner::Peek(const bool advance) {
    int chr;
    int err;

    do {
        if ((chr = this->ibuf_.Peek(advance)) > 0)
            break;

        if (chr == 0) {
            this->status = ScannerStatus::INVALID_NULLBYTE;

            return -1;
        }

        if (this->fd_ == nullptr) {
            this->status = ScannerStatus::END_OF_FILE;

            return -1;
        }

        if (this->prompt_ != nullptr)
            err = this->UnderflowInteractive();
        else
            err = this->ibuf_.ReadFile(this->fd_);

        if (err == 0) {
            this->status = ScannerStatus::END_OF_FILE;

            return -1;
        }

        if (err < 0) {
            this->status = err == -2 ? ScannerStatus::IO_ERROR : ScannerStatus::NOMEM;

            return -1;
        }

        chr = err;
    } while (chr > 0);

    if (advance && chr > 0)
        this->loc.Advance(chr == '\n');

    return chr;
}

int Scanner::UnderflowInteractive() {
    const auto err = this->promptfn_(this->prompt_, this->fd_, &this->ibuf_);
    if (err < 0)
        return err;

    if (this->next_prompt_ != nullptr)
        this->prompt_ = this->next_prompt_;

    return err;
}

const char *Scanner::GetStatusMessage() const {
    static const char *messages[] = {
        "empty '' not allowed",
        "end of file reached",
        "invalid digit in binary literal",
        "byte string can only contain ASCII literal characters",
        "can't decode bytes in unicode sequence, escape format must be: \\Uhhhhhhhh",
        "can't decode bytes in unicode sequence, escape format must be: \\uhhhh",
        "can't decode byte, hex escape must be: \\xhh",
        "invalid hexadecimal literal",
        "expected new-line after line continuation character",
        "invalid digit in number",
        "invalid digit in octal literal",
        "octal escape value out of range, must be between \\000 and \\377",
        "unterminated string",
        "invalid raw string prologue",
        "expected '",
        "unterminated string",
        "invalid token",
        "illegal Unicode character",
        "invalid unsigned qualifier here",
        "null-byte '\\0' is not allowed in source code",
        "input/output error while reading source",
        "not enough memory",
        "ok"
    };

    return messages[(int) this->status];
}

Scanner::Scanner(orbiter::Isolate *isolate, FILE *fd, const char *ps1, const char *ps2,
                 int buf_size) noexcept : isolate_(isolate),
                                          prompt_(ps1),
                                          next_prompt_(ps2),
                                          fd_(fd),
                                          sbuf_(isolate),
                                          ibuf_(isolate, buf_size) {
    assert(buf_size > 0);

    this->promptfn_ = DefaultPrompt;
}
