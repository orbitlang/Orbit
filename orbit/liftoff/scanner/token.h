// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_SCANNER_TOKEN_H_
#define ORBIT_LIFTOFF_SCANNER_TOKEN_H_

#include <orbit/datatype.h>

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
        SUPER,

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
        DT_UNIT,
        DT_OPAQUE,
        DT_PTR,

        DT_F32,
        DT_F64,
        DATATYPE_END,

        // Comments
        COMMENT_BEGIN,
        COMMENT,
        COMMENT_DOC,
        COMMENT_INLINE,
        COMMENT_MODULE,
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
        KW_CATCH,
        KW_CASE,
        KW_CLASS,
        KW_CLEANUP,
        KW_CONST,
        KW_CONTINUE,
        KW_DEFAULT,
        KW_DEFER,
        KW_ELIF,
        KW_ELSE,
        KW_FALLTHROUGH,
        KW_FINALLY,
        KW_FOR,
        KW_FROM,
        KW_FUNC,
        KW_IF,
        KW_IN,
        KW_INIT,
        KW_IMPL,
        KW_IMPORT,
        KW_LET,
        KW_LOOP,
        KW_NAMESPACE,
        KW_NATIVE,
        KW_NEW,
        KW_NOT,
        KW_OF,
        KW_PANIC,
        KW_PUB,
        KW_PROT,
        KW_RETURN,
        KW_YIELD,
        KW_SPAWN,
        KW_SWITCH,
        KW_SYNC,
        KW_TRAIT,
        KW_TRAP,
        KW_TRY,
        KW_VAR,
        KW_WEAK,
        KW_WHEN,
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

        COMPOUND_ASSIGN_BEGIN,
        ASSIGN_ADD,
        ASSIGN_SUB,
        ASSIGN_MUL,
        ASSIGN_SLASH,
        COMPOUND_ASSIGN_END,

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
        DECORATOR,
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

        void Advance(const bool nl) {
            this->offset++;
            this->column++;

            if (nl) {
                this->line++;
                this->column = 1;
            }
        }
    };

    struct Loc {
        /**
         * @brief Tracks the starting position within a source code file.
         *
         * This variable is an instance of the `scanner::Position` struct and represents
         * the starting column, line, and offset in the source code during parsing.
         */
        Position start;

        /**
         * @brief Tracks the ending position within a source code file.
         *
         * This variable is an instance of the `scanner::Position` struct and represents
         * the ending column, line, and offset in the source code during parsing.
         */
        Position end;
    };

    class Token {
    public:
        /**
        * @brief Pointer to an instance of the orbiter::Isolate class.
        *
        * This variable represents a null-initialized pointer to an `orbiter::Isolate` instance.
        * The variable is typically used when interaction with isolated environments is required,
        * and provides a mechanism to reference or manipulate those individual states.
        */
        orbiter::Isolate *isolate = nullptr;

        /**
         * @brief Pointer to a dynamically allocated buffer.
         *
         * This buffer is used to store data related to the Token instance.
         * It may be allocated or freed during the lifecycle of the Token.
         * The ownership and lifetime of this buffer are managed by the `Token` class,
         * ensuring proper memory handling.
         */
        unsigned char *buffer = nullptr;

        /// @brief Represents the length of a buffer or string in terms of the number of characters or bytes.
        MSize length = 0;

        /**
         * Represents the type of token in the source code.
         *
         * The 'type' variable is initialized to `TokenType::TK_NULL`, indicating that the token currently has no assigned type.
         * TokenType is an enumeration that classifies tokens based on their syntactical or functional role in the source code.
         */
        TokenType type = TokenType::TK_NULL;

        /**
         * Represents the location of a token or syntactic element within the source code.
         *
         * This variable holds a `Loc` object which defines the starting and ending positions of the token or element.
         * The `start` and `end` positions contain offset and line information to facilitate precise source code mapping.
         */
        Loc loc{};

        Token() = default;

        Token(Token &other) = delete;

        Token(Token &&other) noexcept: isolate(other.isolate),
                                       buffer(other.buffer),
                                       length(other.length),
                                       type(other.type),
                                       loc(other.loc) {
            other.buffer = nullptr;
            other.length = 0;
        }

        ~Token() {
            this->type = TokenType::TK_NULL;

            if (this->isolate != nullptr && this->buffer != nullptr) {
                const orbiter::memory::IsolateAllocator allocator(this->isolate);
                allocator.free(this->buffer);
            }

            this->buffer = nullptr;
        }

        Token &operator=(const Token &other) = delete;

        Token &operator=(Token &&other) noexcept {
            this->isolate = other.isolate;
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
