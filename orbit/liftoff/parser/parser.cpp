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
        case TokenType::EQUAL:
        case TokenType::ASSIGN_ADD:
        case TokenType::ASSIGN_SUB:
            return 20;
        case TokenType::COMMA:
            return 25;
        case TokenType::ELVIS:
        case TokenType::QUESTION:
        case TokenType::NULL_COALESCING:
            return 30; // Conditional operators
        case TokenType::PIPELINE:
            return 40;
        case TokenType::OR:
            return 50;
        case TokenType::AND:
            return 60;
        case TokenType::PIPE:
            return 70;
        case TokenType::CARET:
            return 80;
        case TokenType::AMPERSAND:
            return 90;
        case TokenType::EQUAL_EQUAL:
        case TokenType::EQUAL_STRICT:
        case TokenType::KW_IN:
        case TokenType::KW_NOT:
        case TokenType::NOT_EQUAL:
        case TokenType::NOT_EQUAL_STRICT:
            return 100; // Equality and membership operators
        case TokenType::LESS:
        case TokenType::LESS_EQ:
        case TokenType::GREATER:
        case TokenType::GREATER_EQ:
            return 110; // Comparison operators
        case TokenType::ARROW_LEFT:
        case TokenType::ARROW_RIGHT:
            return 120; // Channel operators
        case TokenType::SHL:
        case TokenType::SHR:
            return 130; // Bit shift operators
        case TokenType::PLUS:
        case TokenType::MINUS:
            return 140; // Additive operators
        case TokenType::ASTERISK:
        case TokenType::SLASH:
        case TokenType::SLASH_SLASH:
        case TokenType::PERCENT:
            return 150; // Multiplicative operators

        case TokenType::DOT:
        case TokenType::QUESTION_DOT:
            return 170; // Member access
        case TokenType::PLUS_PLUS:
        case TokenType::MINUS_MINUS:
            return 180; // Postfix increment/decrement
        default:
            return 1000; // Highest precedence for unknown tokens
    }
}

ASTHandle<ASTNode *> Parser::ParseAssignment(ASTHandle<ASTNode *> &left) {
    const auto tk_type = TKCUR_TYPE;

    this->Eat(true);

    // TODO
    if (left->node_type != NodeType::IDENTIFIER
        // && left->node_type != NodeType::INDEX
        // && left->node_type != NodeType::SLICE
        && left->node_type != NodeType::TUPLE)
        //&& left->node_type != NodeType::SELECTOR)
        throw ParserException(1);

    // Check for tuple content
    if (left->node_type == NodeType::TUPLE) {
        const auto tuple = (ListExpression *) left.get();

        for (auto &cursor: tuple->elements) {
            const auto *itm = cursor.get();

            if (itm->node_type != NodeType::IDENTIFIER)
                //&& itm->node_type != NodeType::INDEX
                //&& left->node_type != NodeType::SLICE
                //&& itm->node_type != NodeType::SELECTOR)
                throw ParserException(1);
        }
    }

    auto expr = this->ParseExpression(tk_type);

    auto assign = MakeAssignment(TKCUR_LOC);

    assign->name = left.release();
    assign->value = expr.release();

    assign->loc.start = assign->name->loc.start;
    assign->loc.end = assign->value->loc.end;

    return assign;
}

ASTHandle<ASTNode *> Parser::ParseAPST() {
    const auto start_pos = TKCUR_LOC.start;
    const auto tk_type = TKCUR_TYPE;

    auto node_type = NodeType::ASSIGNMENT;
    switch (tk_type) {
        case TokenType::KW_AWAIT:
            node_type = NodeType::AWAIT;
            break;
        case TokenType::KW_PANIC:
            node_type = NodeType::PANIC;
            break;
        case TokenType::KW_SPAWN:
            node_type = NodeType::SPAWN;
            break;
        case TokenType::KW_TRAP:
            node_type = NodeType::TRAP;
            break;
        default:
            assert(false);
    }

    this->Eat(true);

    auto right = this->ParseExpression(TokenType::COMMA);

    if (right->node_type == node_type)
        return right;

    auto &unary = (ASTHandle<Unary *> &) right;
    if (unary->node_type == NodeType::NIL_SAFE && unary->value->node_type == node_type) {
        unary->loc.start = start_pos;

        return right;
    }

    auto ret = MakeUnary(TKCUR_LOC, node_type);

    ret->loc.start = start_pos;
    ret->loc.end = right->loc.end;

    if (right->node_type == NodeType::NIL_SAFE) {
        auto &unary = (ASTHandle<Unary *> &) right;

        ret->value = unary->value;

        unary->loc.start = start_pos;
        unary->value = ret.release();

        return right;
    }

    ret->value = right.release();

    return ret;
}

ASTHandle<ASTNode *> Parser::ParseDictSet() {
    std::vector<ASTHandle<ASTNode *> > elements;

    const auto loc = TKCUR_LOC;
    auto node_type = NodeType::ASSIGNMENT;

    this->Eat(true);

    if (this->Match(TokenType::RIGHT_BRACES)) {
        auto ret = MakeListExpression(TKCUR_LOC, NodeType::DICT);

        ret->loc.start = loc.start;
        ret->loc.end = TKCUR_LOC.end;

        this->Eat(false);

        return ret;
    }

    do {
        elements.push_back(this->ParseExpression(TokenType::COMMA));

        if (this->MatchEat(TokenType::COLON, true)) {
            if (node_type == NodeType::SET)
                throw ParserException(7);

            node_type = NodeType::DICT;

            this->EatNL();

            elements.push_back(this->ParseExpression(TokenType::COMMA));

            continue;
        }

        if (node_type == NodeType::DICT)
            throw ParserException(6);

        node_type = NodeType::SET;
    } while (this->MatchEat(TokenType::COMMA, true));

    auto ret = MakeListExpression(TKCUR_LOC, node_type);

    if (!this->MatchEat(TokenType::RIGHT_BRACES, false))
        throw ParserException(node_type == NodeType::DICT ? 8 : 9);

    ret->loc.start = loc.start;

    ret->elements = std::move(elements);

    return ret;
}

ASTHandle<ASTNode *> Parser::ParseElvis(ASTHandle<ASTNode *> &left) {
    this->Eat(true);

    auto expr = this->ParseExpression(TokenType::COMMA);

    auto elvis = MakeBinary(TKCUR_LOC, NodeType::ELVIS);

    elvis->left = left.release();
    elvis->right = expr.release();

    elvis->loc.start = elvis->left->loc.start;
    elvis->loc.end = elvis->right->loc.end;

    return elvis;
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

ASTHandle<ASTNode *> Parser::ParseIndexing(ASTHandle<ASTNode *> &left) {
    auto subscript = MakeSubscript(TKCUR_LOC, NodeType::INDEX);

    subscript->expression = left.release();

    this->Eat(true);

    if (this->Match(TokenType::RIGHT_SQUARE))
        throw ParserException(3);

    if (!this->Match(TokenType::COLON))
        subscript->start = this->ParseExpression().release();

    if (this->MatchEat(TokenType::COLON, true)) {
        subscript->node_type = NodeType::SLICE;

        if (!this->Match(TokenType::COLON, TokenType::RIGHT_SQUARE))
            subscript->stop = this->ParseExpression().release();

        if (this->MatchEat(TokenType::COLON, true)) {
            if (!this->Match(TokenType::RIGHT_SQUARE))
                subscript->step = this->ParseExpression().release();
        }
    }

    this->EatNL();

    subscript->loc.end = TKCUR_LOC.end;

    if (!this->Match(TokenType::RIGHT_SQUARE))
        throw ParserException(4);

    this->Eat(false);

    subscript->loc.start = subscript->expression->loc.start;

    return subscript;
}

ASTHandle<ASTNode *> Parser::ParseInfix(ASTHandle<ASTNode *> &left) {
    const auto tk_type = TKCUR_TYPE;

    this->Eat(true);

    auto right = this->ParseExpression(tk_type);

    auto infix = MakeBinary(TKCUR_LOC, NodeType::BINARY);

    infix->token_type = tk_type;

    infix->loc.start = left->loc.start;
    infix->loc.end = right->loc.end;

    infix->left = left.release();
    infix->right = right.release();

    return infix;
}

ASTHandle<ASTNode *> Parser::ParseInNotIn(ASTHandle<ASTNode *> &left) {
    auto node_type = NodeType::IN;

    if (TKCUR_TYPE == TokenType::KW_NOT) {
        node_type = NodeType::NOT_IN;

        this->Eat(true);
    }

    if (!this->MatchEat(TokenType::KW_IN, true))
        throw ParserException(0);

    auto binary = MakeBinary(TKCUR_LOC, node_type);

    binary->left = left.release();
    binary->right = this->ParseExpression(TokenType::KW_IN).release();

    binary->loc.start = binary->left->loc.start;
    binary->loc.end = binary->right->loc.end;

    return binary;
}

ASTHandle<ASTNode *> Parser::ParseList() {
    auto list = MakeListExpression(TKCUR_LOC, NodeType::LIST);

    this->Eat(true);

    if (!this->Match(TokenType::RIGHT_SQUARE)) {
        do {
            this->EatNL();

            list->elements.push_back(this->ParseExpression(TokenType::COMMA));
        } while (this->MatchEat(TokenType::COMMA, true));
    }

    list->loc.end = TKCUR_LOC.end;

    if (!this->MatchEat(TokenType::RIGHT_SQUARE, false))
        throw ParserException(5);

    return list;
}

ASTHandle<ASTNode *> Parser::ParseExpression(int precedence) {
    ASTHandle<ASTNode *> left;

    LedMeth led;
    NudMeth nud;

    if ((nud = LookupNUD(TKCUR_TYPE)) == nullptr) {
        assert(false);
    }

    left = (this->*nud)();

    bool is_safe = false;
    while (precedence < PeekPrecedence(TKCUR_TYPE)) {
        if ((led = LookupLED(TKCUR_TYPE)) == nullptr)
            break;

        if (TKCUR_TYPE == TokenType::QUESTION_DOT)
            is_safe = true;

        left = (this->*led)(left);
    }

    if (is_safe) {
        auto safe = MakeUnary(TKCUR_LOC, NodeType::NIL_SAFE);

        safe->loc = left->loc;
        safe->value = left.release();

        return safe;
    }

    return left;
}

ASTHandle<ASTNode *> Parser::ParseExpression(TokenType precedence) {
    return this->ParseExpression(PeekPrecedence(precedence));
}

ASTHandle<ASTNode *> Parser::ParseExpressionList(ASTHandle<ASTNode *> &left) {
    auto list = MakeListExpression(TKCUR_LOC, NodeType::TUPLE);

    list->loc.start = left->loc.start;

    this->Eat(false);

    list->elements.emplace_back(left.release());

    Position end{};
    do {
        this->EatNL();

        auto expr = this->ParseExpression(TokenType::COMMA);

        end = expr->loc.end;

        list->elements.emplace_back(expr.release());

        this->IgnoreNewLineIF(TokenType::COMMA);
    } while (this->MatchEat(TokenType::COMMA, false));

    list->loc.end = end;

    return list;
}

ASTHandle<ASTNode *> Parser::ParseExprOrTuple() {
    auto tuple = MakeListExpression(TKCUR_LOC, NodeType::LIST);

    this->Eat(true);

    if (!this->Match(TokenType::RIGHT_ROUND)) {
        do {
            this->EatNL();

            tuple->elements.push_back(this->ParseExpression(TokenType::COMMA));
        } while (this->MatchEat(TokenType::COMMA, true));
    }

    tuple->loc.end = TKCUR_LOC.end;

    if (!this->MatchEat(TokenType::RIGHT_ROUND, false))
        throw ParserException(10);

    if (tuple->elements.size() == 1)
        return std::move(tuple->elements.back());

    return tuple;
}

ASTHandle<ASTNode *> Parser::ParseFunc() {
    return this->ParseFunction(true);
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

ASTHandle<ASTNode *> Parser::ParseMemberAccess(ASTHandle<ASTNode *> &left) {
    auto selector = MakeBinary(TKCUR_LOC, NodeType::SELECTOR);

    selector->token_type = TKCUR_TYPE;
    selector->loc.start = left->loc.start;

    selector->left = left.release();

    this->Eat(true);

    selector->right = this->ParseIdentifier().release();
    if (selector->right == nullptr)
        throw ParserException(0);

    selector->loc.end = selector->right->loc.end;

    return selector;
}

ASTHandle<ASTNode *> Parser::ParseNullCoalescing(ASTHandle<ASTNode *> &left) {
    this->Eat(true);

    auto expr = this->ParseExpression(TokenType::NULL_COALESCING);

    auto binary = MakeBinary(TKCUR_LOC, NodeType::NULL_COALESCING);

    binary->loc.start = left->loc.start;
    binary->loc.end = expr->loc.end;

    binary->left = left.release();
    binary->right = expr.release();

    return binary;
}

ASTHandle<ASTNode *> Parser::ParsePostInc(ASTHandle<ASTNode *> &left) {
    // TODO: post_inc
    if (left->node_type != NodeType::IDENTIFIER)
        //&& left->node_type != NodeType::INDEX
        //&& left->node_type != NodeType::SELECTOR)
        throw ParserException(2);

    auto update = MakeUnary(TKCUR_LOC, NodeType::UPDATE);

    update->token_type = TKCUR_TYPE;

    update->loc.start = left->loc.start;

    update->value = left.release();

    this->Eat(false);

    return update;
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

ASTHandle<ASTNode *> Parser::ParseTernary(ASTHandle<ASTNode *> &left) {
    auto branch = MakeBranch(TKCUR_LOC);

    branch->test = left.release();

    branch->loc.start = left->loc.start;

    this->Eat(true);

    branch->body = this->ParseExpression(TokenType::COMMA).release();

    branch->loc.end = branch->body->loc.end;

    this->IgnoreNewLineIF(TokenType::COLON);

    if (this->MatchEat(TokenType::COLON, false)) {
        this->EatNL();

        branch->orelse = this->ParseExpression(TokenType::COMMA).release();

        branch->loc.end = branch->orelse->loc.end;
    }

    return branch;
}

ASTHandle<liftoff::parser::Function *> Parser::ParseFunction(bool inl) {
    Context ctx(this, ContextType::FUNC);

    auto func = MakeFunction(TKCUR_LOC);

    this->Eat(true);

    if (!inl) {
        if (!this->Match(TokenType::IDENTIFIER))
            throw ParserException(16);

        auto id_name = ORStringNew(this->ctx_, this->tkcur_.buffer, this->tkcur_.length);
        if (!id_name)
            throw DatatypeException();

        func->name = id_name.release();
    } else {
        func->name = this->MakeFuncName().release();
        func->anon = true;
    }

    if (this->sym_t_->DeclareSymbolScope(func->name, SymbolType::FUNC, TKCUR_START.offset, TKCUR_START.line) == nullptr)
        throw SymbolTableException();

    func->params = this->ParseFuncParams();

    if (this->Match(TokenType::COLON)) {
        // TODO: parse ret_type
        this->Eat(true);
        assert(false);
    }

    while (TKCUR_TYPE > TokenType::KEYWORD_BEGIN && TKCUR_TYPE < TokenType::KEYWORD_END) {
        if (this->MatchEat(TokenType::KW_ASYNC, true))
            func->async = true;
        else if (this->MatchEat(TokenType::KW_CONST, true)) {
            if (!this->context_->CheckExt(ContextType::CLASS) && !this->context_->CheckExt(ContextType::TRAIT))
                throw ParserException(17);

            func->constant = true;
        } else
            throw ParserException(0);
    }

    func->body = this->ParseBlock(func->loc.end, false);

    this->sym_t_->scope->line_end = func->loc.end.line;
    this->sym_t_->LeaveScope();

    return func;
}

ASTHandle<Parameter *> Parser::ParseParameter(const Position &start, NodeType type) {
    if (!this->Match(TokenType::IDENTIFIER))
        throw ParserException(0); // TODO: error

    auto id_name = ORStringNew(this->ctx_, this->tkcur_.buffer, this->tkcur_.length);
    if (!id_name)
        throw DatatypeException();

    auto param = MakeParameter(TKCUR_LOC, NodeType::KW_PARAM);

    this->Eat(false);

    param->id = id_name.release();
    param->loc.start = start;

    if (type == NodeType::PARAM && this->MatchEat(TokenType::EQUAL, true)) {
        param->node_type = NodeType::DEF_PARAM;

        if (!this->Match(TokenType::COMMA, TokenType::RIGHT_ROUND)) {
            param->value = this->ParseExpression(TokenType::COMMA).release();
            param->loc.end = param->value->loc.end;
        }
    }

    return param;
}

std::vector<ASTHandle<ASTNode *> > Parser::ParseBlock(Position &end, bool nested) {
    std::vector<ASTHandle<ASTNode *> > statements;

    if (!this->MatchEat(TokenType::LEFT_BRACES, true))
        throw ParserException(18);

    if (nested)
        this->sym_t_->EnterNestedScope();

    while (!this->Match(TokenType::RIGHT_BRACES)) {
        statements.push_back(this->ParseStatement());

        if (!this->Match(TokenType::END_OF_LINE, TokenType::SEMICOLON, TokenType::RIGHT_BRACES))
            throw ParserException(0);

        while (this->MatchEat(TokenType::SEMICOLON, true));
    }

    if (!this->MatchEat(TokenType::RIGHT_BRACES, true))
        throw ParserException(19);

    if (nested)
        this->sym_t_->LeaveNestedScope();

    end = TKCUR_END;

    this->Eat(false);

    return statements;
}

std::vector<ASTHandle<ASTNode *> > Parser::ParseFuncParams() {
    std::vector<ASTHandle<ASTNode *> > params;
    int mode = 0;

    if (!this->Match(TokenType::LEFT_ROUND))
        throw ParserException(11);

    this->Eat(true);

    do {
        auto start = TKCUR_LOC.start;

        if (this->Match(TokenType::RIGHT_ROUND))
            break;

        if (this->Match(TokenType::ASTERISK)) {
            this->Eat(false);

            params.emplace_back(this->ParseParameter(start, NodeType::KW_PARAM));

            break;
        }

        if (this->Match(TokenType::ELLIPSIS)) {
            if (mode > 1)
                throw ParserException(15);

            mode = 2;

            this->Eat(false);

            params.emplace_back(this->ParseParameter(start, NodeType::REST_PARAM));
        } else {
            if (mode > 1)
                throw ParserException(13);

            auto param = this->ParseParameter(start, NodeType::PARAM);
            if (param->node_type == NodeType::DEF_PARAM)
                mode = 1;
            else if (mode > 0)
                throw ParserException(14);

            params.emplace_back(param.release());
        }
    } while (this->MatchEat(TokenType::COMMA, true));

    if (!this->MatchEat(TokenType::RIGHT_ROUND, true))
        throw ParserException(12);

    return std::move(params);
}

Parser::LedMeth Parser::LookupLED(TokenType token) noexcept {
    if (token > TokenType::INFIX_BEGIN && token < TokenType::INFIX_END)
        return &Parser::ParseInfix;

    switch (token) {
        case TokenType::EQUAL:
        case TokenType::ASSIGN_ADD:
        case TokenType::ASSIGN_SUB:
        case TokenType::ASSIGN_MUL:
        case TokenType::ASSIGN_SLASH:
            return &Parser::ParseAssignment;
        case TokenType::COMMA:
            return &Parser::ParseExpressionList;
        case TokenType::DOT:
        case TokenType::QUESTION_DOT:
            return &Parser::ParseMemberAccess;
        case TokenType::ELVIS:
            return &Parser::ParseElvis;
        case TokenType::KW_IN:
        case TokenType::KW_NOT:
            return &Parser::ParseInNotIn;
        case TokenType::LEFT_SQUARE:
            return &Parser::ParseIndexing;
        case TokenType::MINUS_MINUS:
        case TokenType::PLUS_PLUS:
            return &Parser::ParsePostInc;
        case TokenType::NULL_COALESCING:
            return &Parser::ParseNullCoalescing;
        case TokenType::QUESTION:
            return &Parser::ParseTernary;
        case TokenType::WALRUS:
            // TODO: walrus
            assert(false);
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

        // Grouping and composite types
        case TokenType::LEFT_BRACES:
            return &Parser::ParseDictSet;
        case TokenType::LEFT_ROUND:
            return &Parser::ParseExprOrTuple;
        case TokenType::LEFT_SQUARE:
            return &Parser::ParseList;

        // Unary prefix operators
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::EXCLAMATION:
        case TokenType::TILDE:
        case TokenType::ASTERISK:
        case TokenType::AMPERSAND:
            return &Parser::ParsePrefix;

        // Keywords that can start expressions
        case TokenType::KW_AWAIT:
        case TokenType::KW_PANIC:
        case TokenType::KW_SPAWN:
        case TokenType::KW_TRAP:
            return &Parser::ParseAPST;

        case TokenType::KW_FUNC:
            return &Parser::ParseFunc;

        default:
            return nullptr;
    }
}

ASTHandle<ASTNode *> Parser::ParsePrefix() {
    const auto tk_type = TKCUR_TYPE;

    auto prefix = MakeUnary(TKCUR_LOC, NodeType::UNARY);

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

HORString Parser::MakeFuncName() const {
    char buffer[50];

    snprintf(buffer, 49, "func$%d", this->context_->anon_count++);

    return ORStringNew(this->ctx_, buffer);
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

void Parser::IgnoreNewLineIF(scanner::TokenType type) {
    const scanner::Token *peek;

    if (this->tkcur_.type != scanner::TokenType::END_OF_LINE)
        return;

    if (!this->scanner_.PeekToken(&peek))
        throw ScannerException();

    if (peek->type == type)
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
