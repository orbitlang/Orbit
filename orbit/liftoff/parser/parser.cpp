// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/liftoff/exception.h>

#include <orbit/liftoff/parser/parser.h>

using namespace orbiter::datatype;
using namespace liftoff::scanner;
using namespace liftoff::parser;

#define TKCUR_LOC   this->tkcur_.loc
#define TKCUR_TYPE  this->tkcur_.type
#define TKCUR_START this->tkcur_.loc.start
#define TKCUR_END   this->tkcur_.loc.end

/*
void Parser::ParseDoc() {
    StringBuilder builder{};
    StringKind kind;
    MSize len;
    MSize cp_len;

    do {
        if (!this->scanner_.NextToken(&this->tkcur_))
            throw ScannerException();
    } while (this->Match(TokenType::END_OF_LINE));

    while (this->TokenInRange(TokenType::COMMENT_BEGIN, TokenType::COMMENT_END)) {
        if (!builder.Write(this->tkcur_.buffer, this->tkcur_.length, 0))
            throw DatatypeException();

        do {
            if (!this->scanner_.NextToken(&this->tkcur_))
                throw ScannerException();
        } while (this->Match(TokenType::END_OF_LINE));
    }

    auto *buffer = builder.BuildString(nullptr, &len, &cp_len, &kind);
    if (buffer == nullptr && len != 0) {
        assert(false);
        // TODO: ERROR!
    }

    auto str = ORStringNew(this->ctx_, buffer, len, cp_len, kind);
    if (str)
        builder.Release();

    return str;
}
*/

int PeekPrecedence(TokenType token) {
    switch (token) {
        case TokenType::END_OF_LINE:
        case TokenType::END_OF_FILE:
            return -1;

        case TokenType::WALRUS:
            return 10;
        case TokenType::EQUAL:
        case TokenType::ASSIGN_ADD:
        case TokenType::ASSIGN_SUB:
            return 20;
        case TokenType::COMMA:
            return 30;
        case TokenType::ARROW_RIGHT:
            return 40;
        case TokenType::ELVIS:
        case TokenType::QUESTION:
        case TokenType::NULL_COALESCING:
            return 50;
        case TokenType::PIPELINE:
            return 60;
        case TokenType::OR:
            return 70;
        case TokenType::AND:
            return 80;
        case TokenType::PIPE:
            return 90;
        case TokenType::CARET:
            return 100;
        case TokenType::AMPERSAND:
            return 110;
        case TokenType::EQUAL_EQUAL:
        case TokenType::EQUAL_STRICT:
        case TokenType::KW_IN:
        case TokenType::KW_NOT:
        case TokenType::NOT_EQUAL:
        case TokenType::NOT_EQUAL_STRICT:
            return 120;
        case TokenType::LESS:
        case TokenType::LESS_EQ:
        case TokenType::GREATER:
        case TokenType::GREATER_EQ:
            return 130;
        case TokenType::ARROW_LEFT:
            return 140;
        case TokenType::SHL:
        case TokenType::SHR:
            return 150;
        case TokenType::PLUS:
        case TokenType::MINUS:
            return 160;
        case TokenType::ASTERISK:
        case TokenType::SLASH:
        case TokenType::SLASH_SLASH:
        case TokenType::PERCENT:
            return 170;
        case TokenType::DOT:
        case TokenType::QUESTION_DOT:
            return 180;
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS:
            return 190;
        default:
            return 1000;
    }
}

ASTHandle<ASTNode *> Parser::ParseExpression() {
    return this->ParseExpression(0);
}

ASTHandle<ASTNode *> Parser::ParseIdentifier() {
    auto id_name = ORStringNew(this->ctx_, this->tkcur_.buffer, this->tkcur_.length);
    if (!id_name)
        throw DatatypeException();

    auto id = MakeIdentifier(TKCUR_LOC);

    id->value = id_name.release();

    this->Eat(false);

    return id;
}

ASTHandle<ASTNode *> Parser::ParseInfix(ASTHandle<ASTNode *> &left) {
    const auto tk_type = TKCUR_TYPE;

    this->Eat(true);

    auto right = this->ParseExpression(tk_type);

    auto infix = MakeBinary(TKCUR_LOC);

    infix->token_type = tk_type;

    infix->loc.start = left->loc.start;
    infix->loc.end = right->loc.end;

    infix->left = left.release();
    infix->right = right.release();

    return infix;
}

ASTHandle<ASTNode *> Parser::ParseExpression(int precedence) {
    ASTHandle<ASTNode *> left;

    LedMeth led;
    NudMeth nud;

    if ((nud = LookupNUD(TKCUR_TYPE)) == nullptr) {
        assert(false);
    }

    left = (this->*nud)();

    while (precedence < PeekPrecedence(TKCUR_TYPE)) {
        if ((led = LookupLED(TKCUR_TYPE)) == nullptr)
            break;

        left = (this->*led)(left);
    }

    return left;
}

ASTHandle<ASTNode *> Parser::ParseExpression(TokenType precedence) {
    return this->ParseExpression(PeekPrecedence(precedence));
}

ASTHandle<ASTNode *> Parser::ParseLiteral() {
    HOObject handle;

    switch (this->tkcur_.type) {
        case TokenType::ATOM:
            assert(false);
        case TokenType::BYTE_STRING:
            // TODO: ByteString
            assert(false);
        case TokenType::DECIMAL:
            handle = DecimalNew(this->ctx_, (const char *) this->tkcur_.buffer);
            break;
        case TokenType::NUMBER:
            handle = IntNew(this->ctx_, (const char *) this->tkcur_.buffer, 10);
            break;
        case TokenType::NUMBER_BIN:
            handle = IntNew(this->ctx_, (const char *) this->tkcur_.buffer, 2);
            break;
        case TokenType::NUMBER_CHR:
            handle = UIntNew(this->ctx_, StringUTF8ToInt(this->tkcur_.buffer));
            break;
        case TokenType::NUMBER_HEX:
            handle = IntNew(this->ctx_, (const char *) this->tkcur_.buffer, 16);
            break;
        case TokenType::NUMBER_OCT:
            handle = IntNew(this->ctx_, (const char *) this->tkcur_.buffer, 8);
            break;
        case TokenType::STRING:
            handle = ORStringNew(this->ctx_, this->tkcur_.buffer, this->tkcur_.length);
            break;
        case TokenType::FALSE:
            handle = HOObject((OObject *) kOddBallFALSE);
            break;
        case TokenType::TRUE:
            handle = HOObject((OObject *) kOddBallTRUE);
            break;
        case TokenType::U_NUMBER:
            handle = UIntNew(this->ctx_, (const char *) this->tkcur_.buffer, 10);
            break;
        case TokenType::U_NUMBER_BIN:
            handle = UIntNew(this->ctx_, (const char *) this->tkcur_.buffer, 2);
            break;
        case TokenType::U_NUMBER_HEX:
            handle = UIntNew(this->ctx_, (const char *) this->tkcur_.buffer, 16);
            break;
        case TokenType::U_NUMBER_OCT:
            handle = UIntNew(this->ctx_, (const char *) this->tkcur_.buffer, 8);
            break;
        case TokenType::NIL:
            // Orbit NIL is nullptr ;)
            break;
        default:
            assert(false); // Never get here!
    }

    if (!handle && TKCUR_TYPE != TokenType::NIL)
        throw DatatypeException();

    auto literal = MakeLiteral(TKCUR_LOC);

    literal->literal = handle.release();

    this->Eat(false);

    return literal;
}

ASTHandle<ASTNode *> Parser::ParseStatement() {
    auto start = TKCUR_START;

    this->EatNL();

    switch (TKCUR_TYPE) {
        case TokenType::KW_LET:
        case TokenType::KW_VAR:
            assert(false);
        default:
            return this->ParseExpression();
    }

    return {};
}

Parser::LedMeth Parser::LookupLED(TokenType token) noexcept {
    if (token > TokenType::INFIX_BEGIN && token < TokenType::INFIX_END)
        return &Parser::ParseInfix;

    switch (token) {
        default:
            return nullptr;
    }
}

Parser::NudMeth Parser::LookupNUD(TokenType token) noexcept {
    if (token > TokenType::LITERAL_BEGIN && token < TokenType::LITERAL_END)
        return &Parser::ParseLiteral;

    switch (token) {
        // Identifiers and self
        case TokenType::IDENTIFIER:
        case TokenType::SELF:
            return &Parser::ParseIdentifier;

        // Unary prefix operators
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::EXCLAMATION:
        case TokenType::TILDE:
        case TokenType::ASTERISK:
        case TokenType::AMPERSAND:
            return &Parser::ParsePrefix;

        default:
            assert(false);
    }
}

ASTHandle<ASTNode *> Parser::ParsePrefix() {
    const auto tk_type = TKCUR_TYPE;

    auto prefix = MakeUnary(TKCUR_LOC);

    this->Eat(true);

    prefix->token_type = tk_type;

    // NOTE: We use the highest precedence among infix operators (ASTERISK in this case)
    // as the minimum precedence for parsing the prefix expression's value.
    // This ensures that we don't accidentally parse too little of the subsequent expression.
    // For example, in "-a * b", we want to parse "a" as the operand, not "a * b".
    // Using a high precedence guarantees that we'll stop at the first infix operator
    // we encounter, correctly parsing unary expressions.
    prefix->value = this->ParseExpression(TokenType::ASTERISK).release();

    prefix->value->loc.end = prefix->value->loc.end;

    return prefix;
}

void Parser::Eat(bool ignore_nl) {
    if (this->tkcur_.type == TokenType::END_OF_FILE)
        return;

    do {
        if (!this->scanner_.NextToken(&this->tkcur_))
            throw ScannerException();

        if (ignore_nl) {
            while (this->tkcur_.type == TokenType::END_OF_LINE) {
                if (!this->scanner_.NextToken(&this->tkcur_))
                    throw ScannerException();
            }
        }
    } while (this->TokenInRange(scanner::TokenType::COMMENT_BEGIN, scanner::TokenType::COMMENT_END));
}

void Parser::EatNL() {
    if (this->tkcur_.type == TokenType::END_OF_LINE)
        this->Eat(true);
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

ASTHandle<Module *> Parser::Parse() noexcept {
    auto module = MakeModule(TKCUR_LOC);
    if (!module)
        return {};

    const auto r_module = module.get();

    if ((r_module->filename = ORStringNew(this->ctx_, this->filename_).release()) == nullptr)
        return {};

    if ((this->sym_t_ = SymbolTableNew(this->ctx_)) == nullptr)
        return {};

    try {
        Context ctx(this, ContextType::MODULE);
        Loc loc{};

        // LOAD FIRST COMMENT
        this->Eat(false);

        loc.start = TKCUR_START;

        while (!this->Match(TokenType::END_OF_FILE)) {
            r_module->statements.push_back(this->ParseStatement());

            loc.end = TKCUR_END;

            if (!this->Match(TokenType::END_OF_FILE, TokenType::END_OF_LINE, TokenType::SEMICOLON))
                throw ParserException(0);

            while (this->MatchEat(TokenType::SEMICOLON, true));
        }
    } catch (...) {
        assert(false);
    }

    return module;
}
