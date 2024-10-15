// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_SCANNER_TOKEN_H_
#define ORBIT_LIFTOFF_SCANNER_TOKEN_H_

#include <orbit/datatype.h>

#include <orbit/orbiter/memory/memory.h>

namespace liftoff::scanner {
    enum class TokenType {
        // Special tokens
        TK_NULL,
        END_OF_LINE,
        END_OF_FILE,
        BLANK,

        // Identifiers
        IDENTIFIER,
        SELF,

        // Datatypes
        DATATYPE_BEGIN,
        DT_BOOL,
        DT_BYTE,

        DT_I8,
        DT_I16,
        DT_I32,
        DT_I64,
        DT_ISIZE,

        DT_U8,
        DT_U16,
        DT_U32,
        DT_U64,
        DT_USIZE,

        DT_F32,
        DT_F64,
        DATATYPE_END,

        // Comments
        COMMENT_BEGIN,
        COMMENT,
        COMMENT_DOC,
        COMMENT_INLINE,
        COMMENT_END,

        // Literals
        LITERAL_BEGIN,

        // -- Boolean and Nil
        FALSE,
        TRUE,
        NIL,

        // -- Numbers
        NUMBER_BEGIN,
        DECIMAL,
        NUMBER,
        U_NUMBER,
        NUMBER_CHR,
        NUMBER_BIN,
        U_NUMBER_BIN,
        NUMBER_OCT,
        U_NUMBER_OCT,
        NUMBER_HEX,
        U_NUMBER_HEX,
        NUMBER_END,

        // -- Strings
        STRING_BEGIN,
        STRING,
        BYTE_STRING,
        STRING_END,

        // -- Other literals
        ATOM,

        LITERAL_END,

        // Keywords
        KEYWORD_BEGIN,
        KW_AS,
        KW_ASYNC,
        KW_ASSERT,
        KW_AWAIT,
        KW_BREAK,
        KW_CASE,
        KW_CLASS,
        KW_CONTINUE,
        KW_DEFAULT,
        KW_DEFER,
        KW_ELIF,
        KW_ELSE,
        KW_FALLTHROUGH,
        KW_FOR,
        KW_FROM,
        KW_FUNC,
        KW_IF,
        KW_IN,
        KW_IMPL,
        KW_IMPORT,
        KW_LET,
        KW_LOOP,
        KW_NAMESPACE,
        KW_NOT,
        KW_OF,
        KW_PANIC,
        KW_PUB,
        KW_RETURN,
        KW_YIELD,
        KW_SPAWN,
        KW_SWITCH,
        KW_SYNC,
        KW_TRAIT,
        KW_TRAP,
        KW_VAR,
        KW_WEAK,
        KEYWORD_END,

        // Operators
        // -- Infix operators
        INFIX_BEGIN,

        // ---- Arithmetic
        PLUS,
        MINUS,
        ASTERISK,
        SLASH,
        SLASH_SLASH,
        PERCENT,

        // ---- Bitwise
        SHL,
        SHR,
        AMPERSAND,
        CARET,
        PIPE,

        // ---- Comparison
        LESS,
        LESS_EQ,
        GREATER,
        GREATER_EQ,
        EQUAL_EQUAL,
        EQUAL_STRICT,
        NOT_EQUAL,
        NOT_EQUAL_STRICT,

        // ---- Logical
        AND,
        OR,
        INFIX_END,

        // -- Prefix operators
        EXCLAMATION,
        TILDE,

        // -- Postfix operators
        PLUS_PLUS,
        MINUS_MINUS,

        // -- Assignment operators
        EQUAL,
        ASSIGN_MUL,
        ASSIGN_ADD,
        ASSIGN_SUB,
        ASSIGN_SLASH,

        // Delimiters
        LEFT_ROUND,
        RIGHT_ROUND,
        LEFT_SQUARE,
        RIGHT_SQUARE,
        LEFT_BRACES,
        RIGHT_BRACES,

        // Punctuation
        COMMA,
        DOT,
        COLON,
        SEMICOLON,

        // Special operators
        ARROW_LEFT,
        ARROW_RIGHT,
        QUESTION,
        QUESTION_DOT,
        ELVIS,
        NULL_COALESCING,
        PIPELINE,
        ELLIPSIS,
        WALRUS
    };

    using Pos = MSize;

    struct Position {
        Pos column;
        Pos line;
        Pos offset;

        void Advance(bool nl) {
            this->offset++;
            this->column++;

            if (nl) {
                this->line++;
                this->column = 1;
            }
        }
    };

    struct Loc {
        Position start;
        Position end;
    };

    struct Token {
        unsigned char *buffer = nullptr;
        MSize length = 0;

        TokenType type = TokenType::TK_NULL;

        Loc loc{};

        Token() = default;

        Token(Token &other) = delete;

        ~Token() {
            orbiter::memory::Free(this->buffer);
        }

        Token &operator=(const Token &other) = delete;

        Token &operator=(Token &&other) noexcept {
            this->buffer = other.buffer;
            this->length = other.length;
            this->type = other.type;
            this->loc = other.loc;

            other.buffer = nullptr;
            other.length = 0;

            return *this;
        }
    };
}

#endif // !ORBIT_LIFTOFF_SCANNER_TOKEN_H_
