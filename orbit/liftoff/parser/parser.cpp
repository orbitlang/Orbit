// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cctype>

#include <orbit/orbiter/datatype/atom.h>
#include <orbit/orbiter/datatype/bytes.h>
#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/liftoff/exception.h>

#include <orbit/liftoff/parser/context.h>
#include <orbit/liftoff/parser/parser.h>

using namespace orbiter::datatype;
using namespace liftoff::scanner;
using namespace liftoff::parser;

#define TKCUR_LOC   this->tkcur_.loc
#define TKCUR_TYPE  this->tkcur_.type
#define TKCUR_START this->tkcur_.loc.start
#define TKCUR_END   this->tkcur_.loc.end

int PeekPrecedence(const TokenType token) {
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
        case TokenType::KW_IS:
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
        case TokenType::LEFT_ROUND:
            return 160;
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

ASTHandle<ASTNode *> Parser::InjectInit(Loc loc) const {
    loc.end.offset += 1;

    auto func = MakeFunction(this->isolate_, TKCUR_LOC, NodeType::INIT);

    func->loc.start = loc.start;

    func->name = ORStringNew(this->isolate_, kInitMethodName).release();
    func->body = MakeBlock(this->isolate_, loc).release();

    func->method = true;

    auto *sym = this->sym_t_->DeclareSymbolScope(func->name, SymbolType::METHOD, func->loc.start.offset,
                                                 func->loc.start.line);
    if (sym == nullptr)
        throw SymbolTableException();

    sym->access = AccessModifier::PUBLIC;
    sym->flags = SymbolFlags::SYNTHETIC;

    func->params.emplace_back(std::move(this->PushSelfParam(loc)));

    func->symbol = sym;

    this->sym_t_->LeaveScope(loc.end.offset, loc.end.line);

    return func;
}

ASTHandle<ASTNode *> Parser::ParseClassTrait(const AccessModifier access) {
    Context isolate(this, TKCUR_TYPE == TokenType::KW_CLASS ? ContextType::CLASS : ContextType::TRAIT);

    auto ct = MakeConstruct(this->isolate_,
                            TKCUR_LOC,
                            TKCUR_TYPE == TokenType::KW_CLASS
                                ? NodeType::CLASS
                                : NodeType::TRAIT);

    const auto tk_s_offset = TKCUR_LOC.start.offset;
    const auto tk_s_line = TKCUR_LOC.start.line;

    ct->doc = this->GetDocString(false).release();

    this->Eat(true);

    ct->name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();
    if (ct->name == nullptr)
        throw DatatypeException();

    auto *sym = this->sym_t_->DeclareSymbolScope(
        ct->name,
        ct->node_type == NodeType::CLASS ? SymbolType::CLASS : SymbolType::TRAIT,
        tk_s_offset,
        tk_s_line);
    if (sym == nullptr)
        throw SymbolTableException();

    sym->access = access;

    this->Eat(true);

    if (this->MatchEat(TokenType::COLON, true)) {
        if (!this->context_->Check(ContextType::CLASS))
            throw ParserException(42);

        ct->ext = this->ParseExtImpl().release();
    }

    if (this->MatchEat(TokenType::KW_IMPL, true)) {
        do
            ct->impl.push_back(this->ParseExtImpl());
        while (this->MatchEat(TokenType::PLUS, true));
    }

    const auto old_pub = std::move(this->exports);

    try {
        ct->body = this->ParseBlock(false).release();

        ct->loc.end = ct->body->loc.end;

        this->ClassCheck(ct.get());
    } catch (...) {
        this->exports = old_pub;

        throw;
    }

    ct->exports = std::move(this->exports);

    this->exports = old_pub;

    this->sym_t_->LeaveScope(ct->body->loc.end.offset, ct->body->loc.end.line);

    return ct;
}

ASTHandle<ASTNode *> Parser::ParseDecorator() {
    auto doc = this->GetDocString(false);

    auto deco = MakeDecorator(this->isolate_, TKCUR_LOC);

    this->Eat(true);

    if (!this->Match(TokenType::RIGHT_SQUARE)) {
        do {
            auto left = this->ParseExpression(TokenType::ASTERISK);

            if (left->node_type != NodeType::CALL) {
                auto call = MakeCall(this->isolate_, left->loc, NodeType::CALL);

                call->left = left.release();

                deco->decorators.emplace_back(std::move(call));

                continue;
            }

            deco->decorators.emplace_back(std::move(left));
        } while (this->MatchEat(TokenType::COMMA, true));
    }

    if (!this->MatchEat(TokenType::RIGHT_SQUARE, true))
        throw ParserException(44);

    deco->func = this->ParseStatement().release();

    if (deco->func->node_type != NodeType::FUNCTION)
        throw ParserException(45);

    ((Function *) deco->func)->doc = doc.release();

    return deco;
}

ASTHandle<ASTNode *> Parser::ParseExtImpl() {
    auto left = this->ParseExpression(TokenType::ASTERISK);

    if (left->node_type != NodeType::IDENTIFIER && left->node_type != NodeType::SELECTOR)
        throw ParserException(43);

    if (left->node_type == NodeType::SELECTOR) {
        auto ls = (Binary *) left.get();

        while (ls->left->node_type == NodeType::SELECTOR)
            ls = (Binary *) ls->left;

        if (ls->node_type != NodeType::SELECTOR
            || (ls->left->node_type != NodeType::IDENTIFIER
                && ls->left->node_type != NodeType::SELECTOR))
            throw ParserException(43);
    }

    return left;
}

ASTHandle<ASTNode *> Parser::ParseDeferStatement() {
    const auto start = TKCUR_START;

    this->Eat(true);

    auto call = this->ParseExpression(TokenType::COMMA);
    if (call->node_type != NodeType::CALL)
        throw ParserException(26);

    call->loc.start = start;

    call->node_type = NodeType::DEFER;

    return call;
}

ASTHandle<ASTNode *> Parser::ParseForInStatement() {
    Context context(this, ContextType::LOOP);

    auto forIn = MakeLoop(this->isolate_, TKCUR_LOC, NodeType::FOR_IN);

    if (!this->sym_t_->CreateNestedScope(TKCUR_START.offset))
        throw SymbolTableException();

    this->Eat(true);

    if (this->Match(TokenType::KW_VAR)) {
        constexpr Position end{};
        forIn->init = this->ParseVarDecl(end, AccessModifier::PRIVATE, false, false, true).release();
    } else {
        std::vector<ASTHandle<ASTNode *> > ids;

        do {
            ids.emplace_back(this->ParseExpression(TokenType::KW_IN));
        } while (this->MatchEat(TokenType::COMMA, true));

        if (ids.size() > 1) {
            auto tuple = MakeListExpression(this->isolate_, TKCUR_LOC, NodeType::TUPLE);

            tuple->loc.start = ids.front()->loc.start;
            tuple->loc.end = ids.back()->loc.end;

            tuple->elements = std::move(ids);

            forIn->init = tuple.release();
        } else
            forIn->init = ids.front().release();
    }

    if (!this->MatchEat(TokenType::KW_IN, true))
        throw ParserException(29);

    forIn->test = this->ParseExpression(TokenType::COMMA).release();

    forIn->body = this->ParseBlock(false).release();

    forIn->loc.end = forIn->body->loc.end;

    this->sym_t_->LeaveNestedScope(forIn->loc.end.offset);

    return forIn;
}

ASTHandle<ASTNode *> Parser::ParseIfStatement() {
    auto branch = MakeBranch(this->isolate_, TKCUR_LOC, NodeType::IF);

    this->Eat(true);

    branch->test = this->ParseExpression(TokenType::COMMA).release();

    branch->body = this->ParseBlock(true).release();

    auto end = branch->body->loc.end;

    if (this->Match(TokenType::KW_ELIF)) {
        branch->orelse = this->ParseIfStatement().release();
        end = branch->orelse->loc.end;
    } else if (this->MatchEat(TokenType::KW_ELSE, false)) {
        branch->orelse = this->ParseBlock(true).release();
        end = branch->orelse->loc.end;
    }

    branch->loc.end = end;

    return branch;
}

ASTHandle<ASTNode *> Parser::ParseImportStatement(const AccessModifier access) {
    auto imp = MakeImport(this->isolate_, TKCUR_LOC, NodeType::IMPORT);

    this->Eat(true);

    if (this->Match(TokenType::STRING)) {
        HORString alias;

        auto mod_path = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
        if (!mod_path)
            throw DatatypeException();

        imp->path = mod_path.release();

        imp->loc.end = TKCUR_LOC.end;

        this->Eat(false);

        this->IgnoreNewLineIF(TokenType::KW_AS);

        if (this->MatchEat(TokenType::KW_AS, false)) {
            this->EatNL();

            if (!this->Match(TokenType::IDENTIFIER))
                throw ParserException(47);

            alias = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
            if (!alias)
                throw DatatypeException();

            imp->loc.end = TKCUR_LOC.end;

            this->Eat(false);
        }

        this->CheckSetImportAlias(alias, imp.get());

        imp->alias->access = access;

        return imp;
    }

    imp->node_type = NodeType::IMPORT_FROM;

    do {
        auto imp_name = MakeImportName(this->isolate_, TKCUR_LOC);
        HORString alias;

        if (!this->Match(TokenType::IDENTIFIER))
            throw ParserException(46);

        imp_name->name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();
        if (imp_name->name == nullptr)
            throw DatatypeException();

        this->Eat(true);

        if (this->MatchEat(TokenType::KW_AS, true)) {
            if (!this->Match(TokenType::IDENTIFIER))
                throw ParserException(47);

            alias = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
            if (!alias)
                throw DatatypeException();

            this->Eat(true);
        }

        imp_name->alias = this->sym_t_->Declare(!alias ? imp_name->name : alias.get(),
                                                SymbolType::VARIABLE, imp->loc.start.offset);
        if (imp_name->alias == nullptr)
            throw SymbolTableException();

        imp_name->alias->access = access;
        imp_name->alias->flags |= SymbolFlags::CONST | SymbolFlags::INITIALIZED;

        imp->names.emplace_back(std::move(imp_name));
    } while (this->MatchEat(TokenType::COMMA, true));

    if (!this->MatchEat(TokenType::KW_FROM, true))
        throw ParserException(48);

    if (!this->Match(TokenType::STRING))
        throw ParserException(49);

    imp->path = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();

    imp->loc.end = TKCUR_LOC.end;

    this->Eat(false);

    return imp;
}

ASTHandle<ASTNode *> Parser::ParseLoopStatement() {
    Context context(this, ContextType::LOOP);

    auto loop = MakeLoop(this->isolate_, TKCUR_LOC, NodeType::LOOP);

    if (!this->sym_t_->CreateNestedScope(TKCUR_START.offset))
        throw SymbolTableException();

    this->Eat(true);

    if (!this->Match(TokenType::LEFT_BRACES)) {
        if (!this->Match(TokenType::SEMICOLON))
            loop->init = this->ParseExpression().release();

        if (this->MatchEat(TokenType::SEMICOLON, true)) {
            loop->node_type = NodeType::FOR;

            if (!this->MatchEat(TokenType::SEMICOLON, true)) {
                loop->test = this->ParseExpression().release();

                if (!this->MatchEat(TokenType::SEMICOLON, true))
                    throw ParserException(28);
            }

            if (!this->Match(TokenType::LEFT_BRACES))
                loop->inc = this->ParseExpression().release();
        }
    }

    if (loop->node_type == NodeType::LOOP) {
        loop->test = loop->init;
        loop->init = nullptr;
    }

    loop->body = this->ParseBlock(false).release();
    loop->loc.end = loop->body->loc.end;

    this->sym_t_->LeaveNestedScope(loop->loc.end.offset);

    return loop;
}

ASTHandle<ASTNode *> Parser::ParseNativeBlock() {
    auto block = MakeBlock(this->isolate_, TKCUR_LOC);

    if (!this->Match(TokenType::STRING))
        throw ParserException(59);

    const auto mod_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);

    this->Eat(true);

    if (!this->MatchEat(TokenType::LEFT_BRACES, true))
        throw ParserException(91);

    while (!this->Match(TokenType::RIGHT_BRACES)) {
        ASTHandle<ASTNode *> tmp;

        this->EatNL();

        if (this->MatchEat(TokenType::KW_FUNC, false)) {
            tmp = this->ParseNativeFuncStatement(TKCUR_START, true);

            ((NativeFunc *) tmp.get())->mod_name = mod_name.get_inc();
        } else if (this->Match(TokenType::KW_VAR, TokenType::KW_LET)) {
            tmp = this->ParseNativeVarStatement(TKCUR_START, true);

            ((NativeVariable *) tmp.get())->mod_name = mod_name.get_inc();
        } else
            throw ParserException(92);

        block->statements.push_back(std::move(tmp));

        if (!this->Match(TokenType::END_OF_LINE, TokenType::SEMICOLON, TokenType::RIGHT_BRACES))
            throw ParserException(93);

        while (this->MatchEat(TokenType::SEMICOLON, true));
    }

    this->EatNL();

    block->loc.end = TKCUR_START;

    if (!this->MatchEat(TokenType::RIGHT_BRACES, false))
        throw ParserException(94);

    return block;
}

ASTHandle<ASTNode *> Parser::ParseNativeStatement() {
    auto doc = this->GetDocString(false);
    const auto start = TKCUR_LOC.start;

    this->Eat(true);

    if (this->MatchEat(TokenType::KW_FROM, true))
        return this->ParseNativeBlock();

    if (this->MatchEat(TokenType::KW_FUNC, true)) {
        auto func = this->ParseNativeFuncStatement(start, false);

        ((NativeFunc *) func.get())->doc = doc.release();

        return func;
    }

    // ParseNativeVarStatement expects 'var'/'let' as the current token and
    // raises the appropriate error itself if anything else follows 'native'.
    return this->ParseNativeVarStatement(start, false);
}

ASTHandle<ASTNode *> Parser::ParseNativeFuncStatement(const Position &start, const bool ignore_from) {
    auto func = MakeNativeFunc(this->isolate_, TKCUR_LOC);

    func->loc.start = start;

    if (!this->Match(TokenType::IDENTIFIER))
        throw ParserException(50);

    func->native_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();

    this->Eat(true);

    if (!this->MatchEat(TokenType::LEFT_ROUND, true))
        throw ParserException(51);

    if (!this->Match(TokenType::RIGHT_ROUND)) {
        do {
            auto np = MakeNativeParameter(this->isolate_, TKCUR_LOC);

            if (!this->Match(TokenType::IDENTIFIER))
                throw ParserException(52);

            np->name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();

            this->Eat(true);

            if (!this->MatchEat(TokenType::COLON, true))
                throw ParserException(53);

            if (!this->TokenInRange(TokenType::DATATYPE_BEGIN, TokenType::DATATYPE_END))
                throw ParserException(54);

            np->kind = TKCUR_TYPE;

            this->Eat(true);

            func->parameters.emplace_back(std::move(np));
        } while (this->MatchEat(TokenType::COMMA, true));
    }

    if (!this->MatchEat(TokenType::RIGHT_ROUND, true))
        throw ParserException(55);

    if (!this->MatchEat(TokenType::COLON, true))
        throw ParserException(56);

    if (!this->TokenInRange(TokenType::DATATYPE_BEGIN, TokenType::DATATYPE_END))
        throw ParserException(57);

    func->ret_type = TKCUR_TYPE;

    func->loc.end = TKCUR_LOC.end;

    this->Eat(false);

    this->IgnoreNewLineIF(TokenType::KW_AS);

    if (this->MatchEat(TokenType::KW_AS, false)) {
        this->EatNL();

        if (!this->Match(TokenType::IDENTIFIER))
            throw ParserException(58);

        const auto alias = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
        func->alias = this->sym_t_->DeclareSymbolScope(alias.get(), SymbolType::NATIVE_FUNC, TKCUR_LOC.start.offset,
                                                       TKCUR_LOC.start.line);
        if (func->alias == nullptr)
            throw SymbolTableException();

        func->loc.end = TKCUR_LOC.end;

        this->Eat(false);
    } else {
        func->alias = this->sym_t_->DeclareSymbolScope(func->native_name, SymbolType::NATIVE_FUNC,
                                                       func->loc.start.offset, func->loc.start.line);
        if (func->alias == nullptr)
            throw SymbolTableException();
    }

    for (const auto &cursor: func->parameters) {
        const auto *param = (NativeParameter *) cursor.get();

        const auto *sym = this->sym_t_->Declare(param->name, SymbolType::PARAMETER, param->loc.start.offset);
        if (sym == nullptr)
            throw SymbolTableException();
    }

    this->IgnoreNewLineIF(TokenType::KW_FROM);

    if (this->MatchEat(TokenType::KW_FROM, false)) {
        if (ignore_from)
            throw ParserException(95);

        this->EatNL();

        if (!this->Match(TokenType::STRING))
            throw ParserException(59);

        func->mod_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();

        func->loc.end = TKCUR_LOC.end;

        this->Eat(false);
    }

    this->sym_t_->LeaveScope(func->loc.end.offset, func->loc.end.line);

    return func;
}

ASTHandle<ASTNode *> Parser::ParseNativeVarStatement(const Position &start, const bool ignore_from) {
    auto constant = false;

    auto variable = MakeNativeVariable(this->isolate_, TKCUR_LOC, NodeType::NATIVE_VAR);

    variable->loc.start = start;

    if (this->Match(TokenType::KW_VAR))
        this->Eat(true);
    else if (this->MatchEat(TokenType::KW_LET, true))
        constant = true;
    else
        throw ParserException(60);

    if (!this->Match(TokenType::IDENTIFIER))
        throw ParserException(61);

    const auto id_start = TKCUR_LOC.start;

    variable->native_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();

    this->Eat(true);

    if (!this->MatchEat(TokenType::COLON, true))
        throw ParserException(62);

    if (!this->TokenInRange(TokenType::DATATYPE_BEGIN, TokenType::DATATYPE_END))
        throw ParserException(63);

    variable->kind = TKCUR_TYPE;

    this->Eat(false);

    this->IgnoreNewLineIF(TokenType::KW_AS);

    if (this->MatchEat(TokenType::KW_AS, false)) {
        this->EatNL();

        if (!this->Match(TokenType::IDENTIFIER))
            throw ParserException(64);

        const auto alias = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
        variable->alias = this->sym_t_->Declare(alias.get(), SymbolType::NATIVE_VAR, TKCUR_LOC.start.offset);
        if (variable->alias == nullptr)
            throw SymbolTableException();

        this->Eat(false);
    } else {
        variable->alias = this->sym_t_->Declare(variable->native_name, SymbolType::NATIVE_VAR, id_start.offset);
        if (variable->alias == nullptr)
            throw SymbolTableException();
    }

    if (constant)
        variable->alias->flags |= SymbolFlags::CONST;

    this->IgnoreNewLineIF(TokenType::KW_FROM);

    if (this->MatchEat(TokenType::KW_FROM, false)) {
        if (ignore_from)
            throw ParserException(95);

        this->EatNL();

        if (!this->Match(TokenType::STRING))
            throw ParserException(65);

        variable->mod_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();

        this->Eat(false);
    }

    return variable;
}

ASTHandle<ASTNode *> Parser::ParseSwitchCase(const bool as_if) {
    auto sw_case = MakeSwitchCase(this->isolate_, TKCUR_LOC);
    bool no_def = true;
    bool fallthrough = false;

    if (this->Match(TokenType::KW_DEFAULT))
        no_def = false;

    this->Eat(true);

    if (no_def) {
        int idx = 0;
        do {
            if (as_if && idx > 0)
                throw ParserException(40);

            sw_case->tests.push_back(this->ParseExpression(TokenType::EQUAL));

            idx += 1;
        } while (this->MatchEat(TokenType::SEMICOLON, true));
    }

    if (!this->MatchEat(TokenType::COLON, true))
        throw ParserException(37);

    auto block = MakeBlock(this->isolate_, TKCUR_LOC);

    if (!this->sym_t_->CreateNestedScope(TKCUR_START.offset))
        throw SymbolTableException();

    while (!this->Match(TokenType::KW_CASE, TokenType::KW_DEFAULT, TokenType::RIGHT_BRACES)) {
        if (!this->MatchEat(TokenType::KW_FALLTHROUGH, false)) {
            if (fallthrough)
                throw ParserException(38);

            block->statements.push_back(this->ParseStatement());
        } else
            fallthrough = true;

        if (!this->Match(TokenType::END_OF_LINE, TokenType::SEMICOLON))
            throw ParserException(0);

        this->Eat(true);
    }

    this->sym_t_->LeaveNestedScope(block->loc.end.offset);

    sw_case->body = block.release();

    sw_case->loc.end = sw_case->body->loc.end;

    sw_case->fallthrough = fallthrough;

    return sw_case;
}

ASTHandle<ASTNode *> Parser::ParseSwitchStatement() {
    Context context(this, ContextType::SWITCH);

    auto sw = MakeSwitchBlock(this->isolate_, TKCUR_LOC);
    bool def_case = false;

    this->Eat(true);

    if (!this->Match(TokenType::LEFT_BRACES))
        sw->test = this->ParseExpression(TokenType::EQUAL).release();

    if (!this->MatchEat(TokenType::LEFT_BRACES, true))
        throw ParserException(36);

    while (this->Match(TokenType::KW_CASE, TokenType::KW_DEFAULT)) {
        auto sw_case = this->ParseSwitchCase(sw->test == nullptr);

        if (((SwitchBlock *) sw_case.get())->test == nullptr) {
            if (def_case)
                throw ParserException(39);

            def_case = true;
        }

        sw->cases.push_back(std::move(sw_case));
    }

    this->EatNL();

    if (!this->Match(TokenType::RIGHT_BRACES))
        throw ParserException(41);

    sw->loc.end = TKCUR_LOC.end;

    this->Eat(false);

    return sw;
}

ASTHandle<ASTNode *> Parser::ParseSyncStatement() {
    auto sync = MakeBinary(this->isolate_, TKCUR_LOC, NodeType::SYNC_BLOCK);

    this->Eat(true);

    sync->left = this->ParseExpression(TokenType::COMMA).release();
    sync->right = this->ParseBlock(true).release();

    sync->loc.end = sync->right->loc.end;

    return sync;
}

ASTHandle<ASTNode *> Parser::ParseTryCatchFinally() {
    auto tryb = MakeTryBlock(this->isolate_, TKCUR_LOC);

    this->Eat(true);

    tryb->try_block = this->ParseBlock(true).release();

    this->EatNL();

    if (!this->Match(TokenType::KW_CATCH, TokenType::KW_FINALLY))
        throw ParserException(32);

    while (this->Match(TokenType::KW_CATCH)) {
        auto cb = MakeCatchBlock(this->isolate_, TKCUR_LOC);

        this->Eat(true);

        if (this->MatchEat(TokenType::LEFT_ROUND, true)) {
            do {
                if (!this->Match(TokenType::ATOM))
                    throw ParserException(33);

                cb->catches.emplace_back(this->ParseLiteral());
            } while (this->MatchEat(TokenType::PIPE, true));

            if (!this->MatchEat(TokenType::RIGHT_ROUND, true))
                throw ParserException(84);
        } else if (this->Match(TokenType::ATOM))
            cb->catches.emplace_back(this->ParseLiteral());

        this->EatNL();

        if (!this->sym_t_->CreateNestedScope(cb->loc.start.offset))
            throw SymbolTableException();

        if (this->Match(TokenType::IDENTIFIER)) {
            auto err_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
            if (!err_name)
                throw DatatypeException();

            auto *sym = this->sym_t_->Declare(err_name.get(), SymbolType::ECONST, TKCUR_START.offset);
            if (sym == nullptr)
                throw SymbolTableException();

            auto identifier = MakeIdentifier(this->isolate_, TKCUR_LOC);
            identifier->symbol = sym;
            identifier->value = err_name.release();

            this->Eat(false);

            cb->err = identifier.release();
            cb->body = this->ParseBlock(false).release();
        } else
            cb->body = this->ParseBlock(false).release();

        cb->loc.end = cb->body->loc.end;

        this->sym_t_->LeaveNestedScope(cb->loc.end.offset);

        tryb->catches.emplace_back(cb.release());

        this->IgnoreNewLineIF(TokenType::KW_CATCH);
    }

    this->IgnoreNewLineIF(TokenType::KW_FINALLY);

    if (this->Match(TokenType::KW_FINALLY)) {
        this->Eat(true);

        tryb->finally = this->ParseBlock(true).release();
        tryb->loc.end = tryb->finally->loc.end;
    }

    return tryb;
}

ASTHandle<ASTNode *> Parser::ParseVarDecl(const Position &start, const AccessModifier access, const bool constant,
                                          const bool weak, const bool decl_only) {
    std::vector<ASTHandle<ASTNode *> > identifiers;

    auto location = StorageLocation::AUTO;

    if (this->context_->Check(ContextType::LOOP))
        location = StorageLocation::STACK;

    if (constant && (!this->context_->Check(ContextType::CLASS)
                     && !this->context_->Check(ContextType::TRAIT)
                     && !this->context_->Check(ContextType::MODULE)))
        throw ParserException(69);

    this->Eat(true);

    do {
        if (!this->Match(TokenType::IDENTIFIER))
            throw ParserException(16);

        auto id_str = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
        if (!id_str)
            throw DatatypeException();

        if (access != AccessModifier::PRIVATE)
            this->exports.push_back(id_str);

        const auto sym = this->sym_t_->Declare(id_str.get(), SymbolType::VARIABLE, location, TKCUR_LOC.start.offset);
        if (sym == nullptr)
            throw SymbolTableException();

        if (constant)
            sym->flags |= SymbolFlags::CONST;

        sym->access = access;

        auto identifier = MakeIdentifier(this->isolate_, TKCUR_LOC);
        identifier->symbol = sym;
        identifier->value = id_str.release();

        identifiers.emplace_back(std::move(identifier));

        this->Eat(false);

        this->IgnoreNewLineIF(TokenType::COMMA);
    } while (this->MatchEat(TokenType::COMMA, false));

    ASTHandle<ASTNode *> expr{};

    auto decl = MakeAssignment(
        this->isolate_,
        TKCUR_LOC,
        identifiers.size() > 1 ? NodeType::VAR_DECLARATIONS : NodeType::VAR_DECLARATION);


    decl->loc.start = start;

    if (!decl_only) {
        if (!this->Match(TokenType::END_OF_LINE, TokenType::END_OF_FILE, TokenType::SEMICOLON)) {
            if (!this->MatchEat(TokenType::EQUAL, true))
                throw ParserException(0);

            expr = this->ParseExpression(TokenType::WALRUS);
            if (!this->CheckAssignable(expr.get()))
                throw ParserException(82);

            decl->loc.end = expr->loc.end;
        } else if (constant)
            throw ParserException(24);
    }

    for (auto &identifier: identifiers)
        ((Identifier *) identifier.get())->symbol->flags |= SymbolFlags::INITIALIZED;

    if (identifiers.size() == 1)
        decl->name = identifiers.front().release();
    else {
        auto tuple = MakeListExpression(this->isolate_, TKCUR_LOC, NodeType::TUPLE);

        tuple->loc.start = identifiers.front()->loc.start;
        tuple->loc.end = identifiers.back()->loc.end;

        tuple->elements = std::move(identifiers);

        decl->name = tuple.release();
    }

    decl->value = expr.release();

    decl->constant = constant;
    decl->weak = weak;

    return decl;
}

ASTHandle<ASTNode *> Parser::ParseWhenStatement() {
    auto branch = MakeBranch(this->isolate_, TKCUR_LOC, NodeType::WHEN);
    const auto when_start = TKCUR_START;

    this->Eat(true);

    branch->test = this->ParseExpression(TokenType::COMMA).release();

    if (!this->sym_t_->CreateNestedScope(TKCUR_START.offset))
        throw SymbolTableException();

    branch->body = this->ParseBlock(false).release();

    if (!this->sym_t_->LeaveMergeNestedScope(TKCUR_START.offset, when_start.offset))
        throw SymbolTableException();

    auto end = branch->body->loc.end;

    if (this->MatchEat(TokenType::KW_ELSE, false)) {
        this->EatNL();

        if (this->Match(TokenType::KW_WHEN)) {
            branch->orelse = this->ParseWhenStatement().release();
            end = branch->orelse->loc.end;
        } else {
            if (!this->sym_t_->CreateNestedScope(TKCUR_START.offset))
                throw SymbolTableException();

            branch->orelse = this->ParseBlock(false).release();

            if (!this->sym_t_->LeaveMergeNestedScope(TKCUR_START.offset, when_start.offset))
                throw SymbolTableException();

            end = branch->orelse->loc.end;
        }
    }

    branch->loc.end = end;

    return branch;
}

// *********************************************************************************************************************
// EXPRESSIONS
// *********************************************************************************************************************

ASTHandle<ASTNode *> Parser::ParseANPST() {
    const auto start_pos = TKCUR_LOC.start;

    auto node_type = NodeType::ASSIGNMENT;
    switch (TKCUR_TYPE) {
        case TokenType::KW_AWAIT:
            node_type = NodeType::AWAIT;
            break;
        case TokenType::KW_DEFER:
            return this->ParseDeferStatement();
        case TokenType::KW_NEW:
            node_type = NodeType::NEW;
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

    // 'new' requires special treatment: parse the callee allowing member
    // access (.) but not call (), then consume the () explicitly.
    // This ensures that post-construction chains like new Obj().method() bind
    // to the new expression — i.e. (new Obj()).method() — not to the callee.
    if (node_type == NodeType::NEW) {
        auto callee = this->ParseExpression(PeekPrecedence(TokenType::LEFT_ROUND));
        if (!this->Match(TokenType::LEFT_ROUND))
            throw ParserException(78);

        auto call = this->ParseFuncCall(callee);

        auto ret = MakeUnary(this->isolate_, TKCUR_LOC, node_type);
        ret->loc.start = start_pos;
        ret->loc.end = call->loc.end;

        ret->value = call.release();

        return ret;
    }

    auto right = this->ParseExpression(TokenType::COMMA);

    if (right->node_type == node_type)
        return right;

    if (node_type == NodeType::SPAWN && right->node_type != NodeType::CALL)
        throw ParserException(27);

    const auto &unary = (ASTHandle<Unary *> &) right;
    if (unary->node_type == NodeType::NIL_SAFE && unary->value->node_type == node_type) {
        unary->loc.start = start_pos;

        return right;
    }

    auto ret = MakeUnary(this->isolate_, TKCUR_LOC, node_type);

    ret->loc.start = start_pos;
    ret->loc.end = right->loc.end;

    if (right->node_type == NodeType::NIL_SAFE) {
        const auto &unary = (ASTHandle<Unary *> &) right;

        ret->value = unary->value;

        unary->loc.start = start_pos;
        unary->value = ret.release();

        return right;
    }

    ret->value = right.release();

    return ret;
}

ASTHandle<ASTNode *> Parser::ParseAssignment(ASTHandle<ASTNode *> &left) {
    auto node_type = NodeType::ASSIGNMENT;

    const auto tk_type = TKCUR_TYPE;

    this->Eat(true);

    if (left->node_type != NodeType::IDENTIFIER
        && left->node_type != NodeType::INDEX
        && left->node_type != NodeType::SLICE
        && left->node_type != NodeType::TUPLE
        && left->node_type != NodeType::SELECTOR)
        throw ParserException(1);

    // Check for tuple content
    if (left->node_type == NodeType::TUPLE) {
        const auto tuple = (ListExpression *) left.get();

        for (auto &cursor: tuple->elements) {
            const auto *itm = cursor.get();

            if (itm->node_type != NodeType::IDENTIFIER
                && itm->node_type != NodeType::INDEX
                && itm->node_type != NodeType::SLICE
                && itm->node_type != NodeType::SELECTOR)
                throw ParserException(1);
        }

        node_type = NodeType::ASSIGNMENTS;
    }

    auto expr = this->ParseExpression(tk_type);
    if (!this->CheckAssignable(expr.get()))
        throw ParserException(82);

    auto assign = MakeAssignment(this->isolate_, TKCUR_LOC, node_type);

    assign->token_type = tk_type;

    assign->name = left.release();
    assign->value = expr.release();

    assign->loc.start = assign->name->loc.start;
    assign->loc.end = assign->value->loc.end;

    return assign;
}

ASTHandle<ASTNode *> Parser::ParseBlock(const bool nested) {
    auto block = MakeBlock(this->isolate_, TKCUR_LOC);

    if (!this->MatchEat(TokenType::LEFT_BRACES, true))
        throw ParserException(18);

    if (nested && !this->sym_t_->CreateNestedScope(block->loc.start.offset))
        throw SymbolTableException();

    while (!this->Match(TokenType::RIGHT_BRACES)) {
        if (this->context_->Check(ContextType::CLASS)) {
            if (!this->Match(TokenType::KW_CLEANUP, TokenType::KW_FUNC, TokenType::KW_INIT,
                             TokenType::KW_LET, TokenType::KW_PUB, TokenType::KW_PROT, TokenType::KW_VAR))
                throw ParserException(71);
        } else if (this->context_->Check(ContextType::TRAIT)) {
            if (!this->Match(TokenType::KW_FUNC, TokenType::KW_LET, TokenType::KW_PUB, TokenType::KW_PROT))
                throw ParserException(72);
        }

        block->statements.push_back(this->ParseStatement());

        if (!this->Match(TokenType::END_OF_LINE, TokenType::SEMICOLON, TokenType::RIGHT_BRACES))
            throw ParserException(0);

        while (this->MatchEat(TokenType::SEMICOLON, true));
    }

    this->EatNL();

    block->loc.end = TKCUR_START;

    if (!this->MatchEat(TokenType::RIGHT_BRACES, false))
        throw ParserException(19);

    if (nested)
        this->sym_t_->LeaveNestedScope(TKCUR_END.offset);

    return block;
}

ASTHandle<ASTNode *> Parser::ParseCleanupInit(const Position &start, const AccessModifier access) {
    Context isolate(this, ContextType::CDTOR);

    const auto tk_type = TKCUR_TYPE;
    const auto loc = TKCUR_LOC;

    auto func = MakeFunction(this->isolate_, TKCUR_LOC,
                             tk_type == TokenType::KW_CLEANUP
                                 ? NodeType::CLEANUP
                                 : NodeType::INIT);

    func->loc.start = start;

    func->name = ORStringNew(this->isolate_, tk_type == TokenType::KW_CLEANUP ? kCleanupMethodName : kInitMethodName).
            release();
    func->doc = this->GetDocString(false).release();

    func->method = true;

    this->Eat(true);

    auto *sym = this->sym_t_->DeclareSymbolScope(func->name,
                                                 SymbolType::METHOD,
                                                 func->loc.start.offset,
                                                 func->loc.start.line);
    if (sym == nullptr)
        throw SymbolTableException();

    sym->access = access;

    func->params.emplace_back(std::move(this->PushSelfParam(loc)));

    if (tk_type == TokenType::KW_INIT) {
        if (!this->Match(TokenType::LEFT_ROUND))
            throw ParserException(74);

        Loc last_param{};
        auto params = this->ParseFuncParams(last_param);

        func->params.insert(func->params.end(),
                            std::make_move_iterator(params.begin()),
                            std::make_move_iterator(params.end()));
    } else {
        if (this->Match(TokenType::LEFT_ROUND))
            throw ParserException(76);
    }

    if (!this->Match(TokenType::LEFT_BRACES))
        throw ParserException(tk_type == TokenType::KW_CLEANUP ? 75 : 73);

    func->symbol = sym;
    func->body = this->ParseBlock(false).release();
    func->loc.end = func->body->loc.end;

    this->sym_t_->LeaveScope(func->loc.end.offset, func->loc.end.line);

    if (access != AccessModifier::PRIVATE)
        this->exports.emplace_back(func->name);

    return func;
}

ASTHandle<ASTNode *> Parser::ParseDictSet() {
    std::vector<ASTHandle<ASTNode *> > elements;

    const auto loc = TKCUR_LOC;
    auto node_type = NodeType::ASSIGNMENT;

    this->Eat(true);

    if (this->Match(TokenType::RIGHT_BRACES)) {
        auto ret = MakeListExpression(this->isolate_, TKCUR_LOC, NodeType::DICT);

        ret->loc.start = loc.start;
        ret->loc.end = TKCUR_LOC.end;

        this->Eat(false);

        return ret;
    }

    do {
        auto key = this->ParseExpression(TokenType::COMMA);
        if (!this->CheckAssignable(key.get()))
            throw ParserException(83);

        elements.push_back(std::move(key));

        if (this->MatchEat(TokenType::COLON, true)) {
            if (node_type == NodeType::SET)
                throw ParserException(7);

            node_type = NodeType::DICT;

            this->EatNL();

            auto value = this->ParseExpression(TokenType::COMMA);
            if (!this->CheckAssignable(value.get()))
                throw ParserException(83);

            elements.push_back(std::move(value));

            continue;
        }

        if (node_type == NodeType::DICT)
            throw ParserException(6);

        node_type = NodeType::SET;
    } while (this->MatchEat(TokenType::COMMA, true));

    auto ret = MakeListExpression(this->isolate_, TKCUR_LOC, node_type);

    if (!this->MatchEat(TokenType::RIGHT_BRACES, false))
        throw ParserException(node_type == NodeType::DICT ? 8 : 9);

    ret->loc.start = loc.start;

    ret->elements = std::move(elements);

    return ret;
}

ASTHandle<ASTNode *> Parser::ParseElvis(ASTHandle<ASTNode *> &left) {
    this->Eat(true);

    auto expr = this->ParseExpression(TokenType::COMMA);

    auto elvis = MakeBinary(this->isolate_, TKCUR_LOC, NodeType::ELVIS);

    elvis->left = left.release();
    elvis->right = expr.release();

    elvis->loc.start = elvis->left->loc.start;
    elvis->loc.end = elvis->right->loc.end;

    return elvis;
}

ASTHandle<ASTNode *> Parser::ParseExpression() {
    return this->ParseExpression(0);
}

ASTHandle<ASTNode *> Parser::ParseExpression(const int precedence) {
    LedMeth led;
    NudMeth nud;

    if ((nud = LookupNUD(TKCUR_TYPE)) == nullptr)
        throw ParserException(0);

    auto left = (this->*nud)();

    bool is_safe = false;
    while (true) {
        // Implicit line continuation.
        // If the newline is followed by a token that has an LED but no NUD,
        // it's a pure infix operator that can't legally start a statement on
        // its own (`|>`, `.`, `?.`, `??`, `?:`, comparisons, …). In that case
        // we swallow the newline and let the loop continue. Otherwise the
        // newline is a real terminator and we exit.
        //
        // Tokens like `+ - * & ( [` have both LED and NUD, so they fall
        // through to the `break` and keep acting as statement starts —
        // sidesteps the JavaScript-ASI footgun.
        if (this->Match(TokenType::END_OF_LINE)) {
            const Token *peek;

            if (!this->scanner_.PeekToken(&peek))
                throw ScannerException();

            if (LookupLED(peek->type) == nullptr || LookupNUD(peek->type) != nullptr)
                break;

            this->EatNL();
        }

        if (precedence >= PeekPrecedence(TKCUR_TYPE))
            break;

        if ((led = LookupLED(TKCUR_TYPE)) == nullptr)
            break;

        if (TKCUR_TYPE == TokenType::QUESTION_DOT)
            is_safe = true;

        left = (this->*led)(left);
    }

    if (is_safe) {
        auto safe = MakeUnary(this->isolate_, TKCUR_LOC, NodeType::NIL_SAFE);

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
    auto list = MakeListExpression(this->isolate_, TKCUR_LOC, NodeType::TUPLE);

    list->loc.start = left->loc.start;

    this->Eat(false);

    if (!this->CheckAssignable(left.get()))
        throw ParserException(83);

    list->elements.emplace_back(left.release());

    Position end{};
    do {
        this->EatNL();

        auto expr = this->ParseExpression(TokenType::COMMA);
        if (!this->CheckAssignable(expr.get()))
            throw ParserException(83);

        end = expr->loc.end;

        list->elements.emplace_back(expr.release());

        this->IgnoreNewLineIF(TokenType::COMMA);
    } while (this->MatchEat(TokenType::COMMA, false));

    list->loc.end = end;

    return list;
}

ASTHandle<ASTNode *> Parser::ParseExprOrTuple() {
    auto tuple = MakeListExpression(this->isolate_, TKCUR_LOC, NodeType::TUPLE);

    bool single_element = false;

    this->Eat(true);

    if (!this->Match(TokenType::RIGHT_ROUND)) {
        do {
            if (this->Match(TokenType::RIGHT_ROUND)) {
                single_element = true;

                break;
            }

            this->EatNL();

            auto item = this->ParseExpression(TokenType::COMMA);
            if (!this->CheckAssignable(item.get()))
                throw ParserException(83);

            tuple->elements.push_back(std::move(item));
        } while (this->MatchEat(TokenType::COMMA, true));
    }

    tuple->loc.end = TKCUR_LOC.end;

    if (!this->MatchEat(TokenType::RIGHT_ROUND, false))
        throw ParserException(10);

    if (!single_element && tuple->elements.size() == 1)
        return std::move(tuple->elements.back());

    return tuple;
}

ASTHandle<ASTNode *> Parser::ParseFunc() {
    const auto start = TKCUR_START;

    return this->ParseFunction(start, false, AccessModifier::PRIVATE);
}

ASTHandle<ASTNode *> Parser::ParseFuncCall(ASTHandle<ASTNode *> &left) {
    auto call = MakeCall(this->isolate_, TKCUR_LOC, NodeType::CALL);

    call->left = left.release();
    call->loc.start = call->left->loc.start;

    this->Eat(true);

    if (!this->Match(TokenType::RIGHT_ROUND)) {
        Position end{};
        int mode = 0;

        do {
            if (this->Match(TokenType::ASTERISK)) {
                auto unary = MakeUnary(this->isolate_, TKCUR_LOC, NodeType::KW_ARG);

                this->Eat(true);

                unary->value = this->ParseExpression(TokenType::COMMA).release();
                unary->loc.end = unary->value->loc.end;

                end = unary->loc.end;
                call->kwargs = unary.release();

                break;
            }

            auto arg = this->ParseExpression(TokenType::COMMA);

            this->EatNL();

            if (this->Match(TokenType::ELLIPSIS)) {
                if (mode > 1)
                    throw ParserException(21);

                auto unary = MakeUnary(this->isolate_, TKCUR_LOC, NodeType::ELLIPSIS);

                unary->loc.start = arg->loc.start;
                unary->value = arg.release();

                end = unary->loc.end;
                call->rest = unary.release();

                mode = 2;

                this->Eat(true);
            } else if (this->Match(TokenType::EQUAL)) {
                const auto &id = (ASTHandle<Identifier *> &) arg;

                // Sanity check
                if (arg->node_type != NodeType::IDENTIFIER)
                    throw ParserException(20);

                this->Eat(true);

                auto named_param = MakeParameter(this->isolate_, TKCUR_LOC, NodeType::NAMED_ARG);

                named_param->id = id->value;

                id->value = nullptr;

                named_param->loc.start = id->loc.start;

                if (!this->Match(TokenType::COMMA, TokenType::RIGHT_ROUND)) {
                    named_param->value = this->ParseExpression(TokenType::COMMA).release();

                    named_param->loc.end = named_param->value->loc.end;
                }

                mode = 1;

                end = named_param->loc.end;

                call->nargs.emplace_back(std::move(named_param));
            } else {
                if (mode > 0)
                    throw ParserException(21);

                end = arg->loc.end;
                call->args.emplace_back(std::move(arg));
            }
        } while (this->MatchEat(TokenType::COMMA, true));

        call->loc.end = end;
    }

    if (!this->MatchEat(TokenType::RIGHT_ROUND, false))
        throw ParserException(22);

    if (call->left->node_type == NodeType::IDENTIFIER) {
        const auto &id = (ASTHandle<Identifier *> &) call->left;

        if (id->symbol->type == SymbolType::NATIVE_FUNC && id->symbol->defining_scope->GetParameterCount() != call->args
            .size())
            throw ParserException(86);

        if (id->symbol->type == SymbolType::NATIVE_VAR)
            throw ParserException(87);
    }

    return call;
}

ASTHandle<ASTNode *> Parser::ParseIdentifier() {
    auto id_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
    if (!id_name)
        throw DatatypeException();

    Symbol *sym = nullptr;

    auto id = MakeIdentifier(this->isolate_, TKCUR_LOC);

    id->kind = TokenType::IDENTIFIER;

    if (this->Match(TokenType::SUPER)) {
        if (!this->context_->CheckExt(ContextType::CLASS) && !this->context_->CheckExt(ContextType::TRAIT))
            throw ParserException(79);

        sym = this->sym_t_->LookupInsert("self", TKCUR_START.offset);

        id->kind = TokenType::SUPER;
    } else if (this->Match(TokenType::SELF)) {
        sym = this->sym_t_->LookupInsert(id_name.get(), TKCUR_START.offset);

        id->kind = TokenType::SELF;
    } else
        sym = this->sym_t_->LookupInsert(id_name.get(), TKCUR_START.offset);

    if (!sym)
        throw SymbolTableException();

    id->symbol = sym;
    id->value = id_name.release();

    this->Eat(false);

    return id;
}

ASTHandle<ASTNode *> Parser::ParseIndexing(ASTHandle<ASTNode *> &left) {
    auto subscript = MakeSubscript(this->isolate_, TKCUR_LOC, NodeType::INDEX);

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

    auto node_type = NodeType::BINARY;

    this->Eat(true);

    auto right = this->ParseExpression(tk_type);

    switch (tk_type) {
        case TokenType::ARROW_LEFT:
            node_type = NodeType::CHAN_SEND;
            break;
        case TokenType::KW_IS:
            node_type = NodeType::IS;
            break;
        case TokenType::LESS:
        case TokenType::LESS_EQ:
        case TokenType::GREATER:
        case TokenType::GREATER_EQ:
        case TokenType::EQUAL_EQUAL:
        case TokenType::EQUAL_STRICT:
        case TokenType::NOT_EQUAL:
        case TokenType::NOT_EQUAL_STRICT:
            node_type = NodeType::CMPEQ;
            break;
        default:
            break;
    }

    auto infix = MakeBinary(this->isolate_, TKCUR_LOC, node_type);

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

    auto binary = MakeBinary(this->isolate_, TKCUR_LOC, node_type);

    binary->left = left.release();
    binary->right = this->ParseExpression(TokenType::KW_IN).release();

    binary->loc.start = binary->left->loc.start;
    binary->loc.end = binary->right->loc.end;

    return binary;
}

ASTHandle<ASTNode *> Parser::ParseList() {
    auto list = MakeListExpression(this->isolate_, TKCUR_LOC, NodeType::LIST);

    this->Eat(true);

    if (!this->Match(TokenType::RIGHT_SQUARE)) {
        do {
            this->EatNL();

            auto item = this->ParseExpression(TokenType::COMMA);
            if (!this->CheckAssignable(item.get()))
                throw ParserException(83);

            list->elements.push_back(std::move(item));
        } while (this->MatchEat(TokenType::COMMA, true));
    }

    list->loc.end = TKCUR_LOC.end;

    if (!this->MatchEat(TokenType::RIGHT_SQUARE, false))
        throw ParserException(5);

    return list;
}

ASTHandle<ASTNode *> Parser::ParseLiteral() {
    HOObject handle;

    switch (this->tkcur_.type) {
        case TokenType::ATOM:
            handle = AtomNew(this->isolate_, (const char *) this->tkcur_.buffer, this->tkcur_.length);
            break;
        case TokenType::BYTE_STRING:
            handle = BytesNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length, true);
            break;
        case TokenType::DECIMAL:
            handle = DecimalNew(this->isolate_, (const char *) this->tkcur_.buffer);
            break;
        case TokenType::NUMBER:
            handle = IntNew(this->isolate_, (const char *) this->tkcur_.buffer, 10);
            break;
        case TokenType::NUMBER_BIN:
            handle = IntNew(this->isolate_, (const char *) this->tkcur_.buffer, 2);
            break;
        case TokenType::NUMBER_CHR:
            handle = UIntNew(this->isolate_, StringUTF8ToInt(this->tkcur_.buffer));
            break;
        case TokenType::NUMBER_HEX:
            handle = IntNew(this->isolate_, (const char *) this->tkcur_.buffer, 16);
            break;
        case TokenType::NUMBER_OCT:
            handle = IntNew(this->isolate_, (const char *) this->tkcur_.buffer, 8);
            break;
        case TokenType::STRING:
            handle = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
            break;
        case TokenType::FALSE:
            handle = HOObject((OObject *) kOddBallFALSE);
            break;
        case TokenType::TRUE:
            handle = HOObject((OObject *) kOddBallTRUE);
            break;
        case TokenType::U_NUMBER:
            handle = UIntNew(this->isolate_, (const char *) this->tkcur_.buffer, 10);
            break;
        case TokenType::U_NUMBER_BIN:
            handle = UIntNew(this->isolate_, (const char *) this->tkcur_.buffer, 2);
            break;
        case TokenType::U_NUMBER_HEX:
            handle = UIntNew(this->isolate_, (const char *) this->tkcur_.buffer, 16);
            break;
        case TokenType::U_NUMBER_OCT:
            handle = UIntNew(this->isolate_, (const char *) this->tkcur_.buffer, 8);
            break;
        case TokenType::NIL:
            // Orbit NIL is nullptr ;)
            break;
        default:
            assert(false); // Never get here!
    }

    if (!handle && TKCUR_TYPE != TokenType::NIL)
        throw DatatypeException();

    auto literal = MakeLiteral(this->isolate_, TKCUR_LOC);

    literal->literal = handle.release();

    this->Eat(false);

    return literal;
}

ASTHandle<ASTNode *> Parser::ParseMemberAccess(ASTHandle<ASTNode *> &left) {
    auto selector = MakeSelector(this->isolate_, TKCUR_LOC);

    selector->token_type = TKCUR_TYPE;
    selector->loc.start = left->loc.start;

    selector->left = left.release();

    this->Eat(true);

    if (!this->Match(TokenType::IDENTIFIER, TokenType::KW_INIT, TokenType::KW_CLEANUP, TokenType::KW_FROM))
        throw ParserException(0);

    auto id_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
    if (!id_name)
        throw DatatypeException();

    auto id = MakeIdentifier(this->isolate_, TKCUR_LOC);

    id->value = id_name.release();

    this->Eat(false);

    selector->right = id.release();

    selector->loc.end = selector->right->loc.end;

    return selector;
}

ASTHandle<ASTNode *> Parser::ParseNullCoalescing(ASTHandle<ASTNode *> &left) {
    this->Eat(true);

    auto expr = this->ParseExpression(TokenType::NULL_COALESCING);

    auto binary = MakeBinary(this->isolate_, TKCUR_LOC, NodeType::NULL_COALESCING);

    binary->loc.start = left->loc.start;
    binary->loc.end = expr->loc.end;

    binary->left = left.release();
    binary->right = expr.release();

    return binary;
}

ASTHandle<ASTNode *> Parser::ParsePipeline(ASTHandle<ASTNode *> &left) {
    this->Eat(true);

    auto right = this->ParseExpression(TokenType::PIPELINE);

    if (right->node_type == NodeType::CALL) {
        const auto &call = (ASTHandle<Call *> &) right;

        right->loc.start = left->loc.start;

        call->args.insert(call->args.begin(), std::move(left));

        return right;
    }

    auto call = MakeCall(this->isolate_, TKCUR_LOC, NodeType::CALL);

    call->loc.start = left->loc.start;
    call->loc.end = right->loc.end;

    call->left = right.release();
    call->args.push_back(std::move(left));

    return call;
}

ASTHandle<ASTNode *> Parser::ParsePostInc(ASTHandle<ASTNode *> &left) {
    if (left->node_type != NodeType::IDENTIFIER
        && left->node_type != NodeType::INDEX
        && left->node_type != NodeType::SELECTOR)
        throw ParserException(2);

    auto update = MakeUnary(this->isolate_, TKCUR_LOC, NodeType::UPDATE);

    update->token_type = TKCUR_TYPE;

    update->loc.start = left->loc.start;

    update->value = left.release();

    this->Eat(false);

    return update;
}

ASTHandle<ASTNode *> Parser::ParsePrefix() {
    const auto tk_type = TKCUR_TYPE;

    auto prefix = MakeUnary(this->isolate_, TKCUR_LOC,
                            tk_type == TokenType::ARROW_LEFT ? NodeType::CHAN_RECV : NodeType::UNARY);

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

ASTHandle<ASTNode *> Parser::ParseStatement() {
    ASTHandle<ASTNode *> stmt;
    ASTHandle<ASTNode *> label;

    const auto start = TKCUR_START;
    auto access = AccessModifier::PRIVATE;
    bool weak = false;

    while (this->Match(TokenType::KW_PUB, TokenType::KW_PROT, TokenType::KW_WEAK)) {
        if (this->MatchEat(TokenType::KW_PUB, true)) {
            access = AccessModifier::PUBLIC;

            continue;
        }

        if (this->MatchEat(TokenType::KW_PROT, true)) {
            if (!this->context_->Check(ContextType::CLASS) && !this->context_->Check(ContextType::TRAIT))
                throw ParserException(81);

            access = AccessModifier::PROTECTED;

            continue;
        }

        if (this->MatchEat(TokenType::KW_WEAK, true)) {
            if (!this->context_->Check(ContextType::CLASS))
                throw ParserException(25);

            weak = true;
        }
    }

    this->EatNL();

    do {
        switch (TKCUR_TYPE) {
            case TokenType::KW_CONTINUE:
                if (!this->context_->CheckExt(ContextType::LOOP))
                    throw ParserException(66);
            case TokenType::KW_BREAK: {
                if (!this->context_->Check(ContextType::LOOP) && !this->context_->Check(ContextType::SWITCH))
                    throw ParserException(66);

                auto bc = MakeJump(this->isolate_, TKCUR_LOC);

                bc->token_type = TKCUR_TYPE;

                this->Eat(false);

                if (this->Match(TokenType::IDENTIFIER)) {
                    bc->label = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length).release();
                    if (bc->label == nullptr)
                        throw DatatypeException();

                    auto *sym = this->sym_t_->Lookup(bc->label, TKCUR_LOC.start.offset);
                    if (sym == nullptr || sym->type != SymbolType::LABEL)
                        throw ParserException(67);

                    this->Eat(false);
                }

                return bc;
            }
            case TokenType::DECORATOR:
                return this->ParseDecorator();
            case TokenType::KW_CLASS:
            case TokenType::KW_TRAIT:
                return this->ParseClassTrait(access);
            case TokenType::KW_CLEANUP:
            case TokenType::KW_INIT:
                return this->ParseCleanupInit(start, access);
            case TokenType::KW_DEFER:
                return this->ParseDeferStatement();
            case TokenType::KW_FOR:
                stmt = this->ParseForInStatement();
                break;
            case TokenType::KW_FUNC: {
                if (this->context_->Check(ContextType::CLASS) || this->context_->Check(ContextType::TRAIT))
                    stmt = this->ParseFunction(start, true, access);
                else
                    stmt = this->ParseFunction(start, false, access);

                stmt->is_expr = ((Function *) stmt.get())->anon;

                break;
            }
            case TokenType::KW_IF:
                return this->ParseIfStatement();
            case TokenType::KW_IMPORT:
                return this->ParseImportStatement(access);
            case TokenType::KW_LET:
                return this->ParseVarDecl(start, access, true, false, false);
            case TokenType::KW_LOOP:
                stmt = this->ParseLoopStatement();
                break;
            case TokenType::KW_NATIVE:
                return this->ParseNativeStatement();
            case TokenType::KW_SWITCH:
                return this->ParseSwitchStatement();
            case TokenType::KW_YIELD:
                if (!this->context_->CheckEnclosingScope(ContextType::FUNC))
                    throw ParserException(31);

                this->sym_t_->scope->flags |= ScopeFlags::GENERATOR;
                [[fallthrough]];
            case TokenType::KW_RETURN: {
                auto ret = MakeUnary(
                    this->isolate_,
                    TKCUR_LOC,
                    TKCUR_TYPE == TokenType::KW_RETURN ? NodeType::RETURN : NodeType::YIELD);

                this->Eat(false);

                if (!this->Match(TokenType::END_OF_LINE, TokenType::END_OF_FILE, TokenType::SEMICOLON)) {
                    if (this->context_->Check(ContextType::CDTOR))
                        throw ParserException(77);

                    ret->value = this->ParseExpression(TokenType::WALRUS).release();
                    ret->loc.end = ret->value->loc.end;
                } else if (ret->node_type == NodeType::YIELD)
                    throw ParserException(30);

                return ret;
            }
            case TokenType::KW_SYNC:
                return this->ParseSyncStatement();
            case TokenType::KW_TRY:
                return this->ParseTryCatchFinally();
            case TokenType::KW_VAR:
                return this->ParseVarDecl(start, access, false, weak, false);
            case TokenType::KW_WHEN:
                if (!this->context_->Check(ContextType::MODULE))
                    throw ParserException(90);

                return this->ParseWhenStatement();
            default:
                stmt = this->ParseExpression();
                if (stmt->node_type != NodeType::VAR_DECLARATION && stmt->node_type != NodeType::VAR_DECLARATIONS)
                    stmt->is_expr = true;

                break;
        }

        this->IgnoreNewLineIF(TokenType::COLON);

        if (!this->MatchEat(TokenType::COLON, false))
            break;

        this->EatNL();

        if (stmt->node_type != NodeType::IDENTIFIER)
            throw ParserException(33);

        if (label)
            throw ParserException(34);

        label = std::move(stmt);

        this->sym_t_->Lookup(((Identifier *) label.get())->value, label->loc.start.offset)->type = SymbolType::LABEL;
    } while (true);

    if (label) {
        if (stmt->node_type != NodeType::FOR
            && stmt->node_type != NodeType::FOR_IN
            && stmt->node_type != NodeType::LOOP)
            throw ParserException(35);

        auto lbl = MakeLabel(this->isolate_, TKCUR_LOC);

        lbl->loc.start = label->loc.start;
        lbl->loc.end = stmt->loc.end;

        const auto &id = (ASTHandle<Identifier *> &) label;

        lbl->label = id->value;

        id->value = nullptr;

        lbl->statement = stmt.release();

        return lbl;
    }

    if (stmt->node_type == NodeType::VAR_DECLARATION || stmt->node_type == NodeType::VAR_DECLARATIONS)
        this->AdjustInlineExport((Assignment *) stmt.get(), access, weak);

    return stmt;
}

ASTHandle<ASTNode *> Parser::ParseTernary(ASTHandle<ASTNode *> &left) {
    auto ternary = MakeTernary(this->isolate_, TKCUR_LOC);

    ternary->left = left.release();

    ternary->loc.start = ternary->left->loc.start;

    this->Eat(true);

    ternary->middle = this->ParseExpression(TokenType::COMMA).release();

    if (!this->MatchEat(TokenType::COLON, true))
        throw ParserException(89);

    ternary->right = this->ParseExpression(TokenType::COMMA).release();

    ternary->loc.end = ternary->right->loc.end;

    return ternary;
}

ASTHandle<ASTNode *> Parser::ParseWalrus(ASTHandle<ASTNode *> &left) {
    auto node_type = NodeType::VAR_DECLARATION;
    auto location = StorageLocation::AUTO;

    Symbol *sym = nullptr;

    this->Eat(true);

    if (this->context_->Check(ContextType::LOOP))
        location = StorageLocation::STACK;

    if (left->node_type == NodeType::TUPLE) {
        const auto &tuple = (ASTHandle<ListExpression *> &) left;

        for (const auto &cursor: tuple->elements) {
            const auto &id = (ASTHandle<Identifier *> &) cursor;

            if (cursor->node_type != NodeType::IDENTIFIER)
                throw ParserException(23);

            sym = this->sym_t_->Declare(id->value, SymbolType::VARIABLE, location, id->loc.start.offset);
            if (sym == nullptr)
                throw SymbolTableException();

            id->symbol = sym;
        }

        node_type = NodeType::VAR_DECLARATIONS;
    } else if (left->node_type != NodeType::IDENTIFIER)
        throw ParserException(23);

    auto decl = MakeAssignment(this->isolate_, TKCUR_LOC, node_type);

    decl->loc.start = left->loc.start;
    decl->name = left.release();

    decl->value = this->ParseExpression(TokenType::WALRUS).release();
    if (!this->CheckAssignable(decl->value))
        throw ParserException(82);

    decl->loc.end = decl->value->loc.end;
    decl->inl = true;

    if (node_type == NodeType::VAR_DECLARATION) {
        sym = this->sym_t_->Declare(((Identifier *) decl->name)->value, SymbolType::VARIABLE, location,
                                    decl->name->loc.start.offset);
        if (sym == nullptr)
            throw SymbolTableException();

        ((Identifier *) decl->name)->symbol = sym;
    }

    return decl;
}

ASTHandle<Function *> Parser::ParseFunction(const Position &start, const bool must_named, const AccessModifier access) {
    Context isolate(this, ContextType::FUNC);

    const auto loc = TKCUR_LOC;

    auto func = MakeFunction(this->isolate_, loc, NodeType::FUNCTION);

    func->loc.start = start;

    func->doc = this->GetDocString(false).release();

    this->Eat(true);

    if (this->Match(TokenType::IDENTIFIER)) {
        auto id_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
        if (!id_name)
            throw DatatypeException();

        func->name = id_name.release();

        this->Eat(true);
    } else {
        if (must_named)
            throw ParserException(88);

        func->name = this->MakeFuncName().release();
        func->anon = true;
    }

    auto *sym = this->sym_t_->DeclareSymbolScope(func->name,
                                                 SymbolType::FUNC,
                                                 func->loc.start.offset,
                                                 func->loc.start.line);
    if (sym == nullptr)
        throw SymbolTableException();

    if (func->anon)
        sym->flags |= SymbolFlags::ANON;

    sym->access = access;

    if (!func->constant && (this->context_->CheckBack(ContextType::CLASS)
                            || this->context_->CheckBack(ContextType::TRAIT))) {
        func->params.emplace_back(std::move(this->PushSelfParam(loc)));

        func->method = true;

        sym->type = SymbolType::METHOD;
    }

    Loc last_param{};
    auto params = this->ParseFuncParams(last_param);
    func->params.insert(func->params.end(),
                        std::make_move_iterator(params.begin()),
                        std::make_move_iterator(params.end()));

    if (this->Match(TokenType::COLON)) {
        // TODO: parse ret_type
        this->Eat(true);

        throw ParserException(70);
    }

    this->IgnoreNewLineIF(TokenType::KW_ASYNC);
    this->IgnoreNewLineIF(TokenType::KW_CONST);

    while (TKCUR_TYPE > TokenType::KEYWORD_BEGIN && TKCUR_TYPE < TokenType::KEYWORD_END) {
        if (this->MatchEat(TokenType::KW_ASYNC, true))
            func->async = true;
        else if (this->MatchEat(TokenType::KW_CONST, true)) {
            if (!this->context_->CheckBack(ContextType::CLASS) && !this->context_->CheckBack(ContextType::TRAIT))
                throw ParserException(17);

            func->constant = true;
        } else
            throw ParserException(0);
    }

    this->IgnoreNewLineIF(TokenType::LEFT_BRACES);

    if (this->Match(TokenType::LEFT_BRACES)) {
        func->body = this->ParseBlock(false).release();
        func->loc.end = func->body->loc.end;
    } else {
        if (!this->context_->CheckBack(ContextType::CLASS) && !this->context_->CheckBack(ContextType::TRAIT))
            throw ParserException(68);

        func->loc.end = TKCUR_START;
    }

    func->symbol = sym;

    this->sym_t_->LeaveScope(func->loc.end.offset, func->loc.end.line);

    if (access != AccessModifier::PRIVATE && !func->anon)
        this->exports.emplace_back(func->name);

    return func;
}

ASTHandle<Parameter *> Parser::ParseParameter(const Position &start, NodeType type) {
    if (!this->Match(TokenType::IDENTIFIER))
        throw ParserException(16);

    auto id_name = ORStringNew(this->isolate_, this->tkcur_.buffer, this->tkcur_.length);
    if (!id_name)
        throw DatatypeException();

    if (this->sym_t_->Declare(id_name.get(), SymbolType::PARAMETER, TKCUR_START.offset) == nullptr)
        throw SymbolTableException();

    auto param = MakeParameter(this->isolate_, TKCUR_LOC, type);

    this->Eat(false);

    param->id = id_name.release();
    param->loc.start = start;

    if (type == NodeType::PARAM && this->MatchEat(TokenType::EQUAL, true)) {
        param->node_type = NodeType::NAMED_PARAM;

        if (!this->Match(TokenType::COMMA, TokenType::RIGHT_ROUND)) {
            param->value = this->ParseExpression(TokenType::COMMA).release();
            param->loc.end = param->value->loc.end;
        }
    }

    return param;
}

ASTHandle<Parameter *> Parser::PushSelfParam(const Loc &loc) const {
    auto id_name = ORStringNew(this->isolate_, "self");
    if (!id_name)
        throw DatatypeException();

    auto *sym = this->sym_t_->Declare(id_name.get(), SymbolType::PARAMETER, loc.start.offset);
    if (sym == nullptr)
        throw SymbolTableException();

    sym->flags = SymbolFlags::INITIALIZED | SymbolFlags::SELF | SymbolFlags::SYNTHETIC;

    auto param = MakeParameter(this->isolate_, loc, NodeType::PARAM);

    param->id = id_name.release();

    return param;
}

HORString Parser::GetDocString(const bool module_doc) {
    const auto doc_type = module_doc ? TokenType::COMMENT_MODULE : TokenType::COMMENT_DOC;

    if (this->doc_.type != doc_type)
        return {};

    auto str = ORStringNewHoldBuffer(this->isolate_, this->doc_.buffer, this->doc_.length);
    if (!str)
        throw DatatypeException();

    this->doc_.buffer = nullptr;

    return str;
}

std::vector<ASTHandle<ASTNode *> > Parser::ParseFuncParams(Loc &last_param) {
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
            if (param->node_type == NodeType::NAMED_PARAM)
                mode = 1;
            else if (mode > 0)
                throw ParserException(14);

            params.emplace_back(param.release());
        }
    } while (this->MatchEat(TokenType::COMMA, true));

    last_param = TKCUR_LOC;

    if (!this->MatchEat(TokenType::RIGHT_ROUND, false))
        throw ParserException(12);

    return std::move(params);
}

Parser::LedMeth Parser::LookupLED(TokenType token) noexcept {
    if (token > TokenType::INFIX_BEGIN && token < TokenType::INFIX_END)
        return &Parser::ParseInfix;

    switch (token) {
        case TokenType::ARROW_LEFT:
        case TokenType::KW_IS:
            return &Parser::ParseInfix;
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
        case TokenType::LEFT_ROUND:
            return &Parser::ParseFuncCall;
        case TokenType::LEFT_SQUARE:
            return &Parser::ParseIndexing;
        case TokenType::MINUS_MINUS:
        case TokenType::PLUS_PLUS:
            return &Parser::ParsePostInc;
        case TokenType::NULL_COALESCING:
            return &Parser::ParseNullCoalescing;
        case TokenType::PIPELINE:
            return &Parser::ParsePipeline;
        case TokenType::QUESTION:
            return &Parser::ParseTernary;
        case TokenType::WALRUS:
            return &Parser::ParseWalrus;
        default:
            return nullptr;
    }
}

Parser::NudMeth Parser::LookupNUD(TokenType token) noexcept {
    if (token > TokenType::LITERAL_BEGIN && token < TokenType::LITERAL_END)
        return &Parser::ParseLiteral;

    switch (token) {
        // Unary prefix operators
        case TokenType::ARROW_LEFT:
        case TokenType::ASTERISK:
        case TokenType::AMPERSAND:
        case TokenType::EXCLAMATION:
        case TokenType::MINUS:
        case TokenType::PLUS:
        case TokenType::TILDE:
            return &Parser::ParsePrefix;

        // Identifiers and self
        case TokenType::IDENTIFIER:
        case TokenType::SELF:
        case TokenType::SUPER:
        case TokenType::KW_FROM:
        case TokenType::KW_DEFAULT: // In expressions, 'cleanup'/'default'/'init' can be used as a variable name
        case TokenType::KW_INIT:
        case TokenType::KW_CLEANUP:
            return &Parser::ParseIdentifier;

        // Grouping and composite types
        case TokenType::LEFT_BRACES:
            return &Parser::ParseDictSet;
        case TokenType::LEFT_ROUND:
            return &Parser::ParseExprOrTuple;
        case TokenType::LEFT_SQUARE:
            return &Parser::ParseList;

        // Keywords that can start expressions
        case TokenType::KW_AWAIT:
        case TokenType::KW_DEFER:
        case TokenType::KW_NEW:
        case TokenType::KW_PANIC:
        case TokenType::KW_SPAWN:
        case TokenType::KW_TRAP:
            return &Parser::ParseANPST;

        case TokenType::KW_FUNC:
            return &Parser::ParseFunc;

        default:
            return nullptr;
    }
}

HORString Parser::MakeFuncName() const {
    char buffer[50];

    snprintf(buffer, 49, "func$%d", this->context_->anon_count++);

    return ORStringNew(this->isolate_, buffer);
}

void Parser::AdjustInlineExport(const Assignment *decl, AccessModifier access, bool weak) {
    if (!decl->inl)
        return;

    if (access == AccessModifier::PUBLIC) {
        if (decl->node_type == NodeType::VAR_DECLARATION) {
            const auto *id = (const Identifier *) decl->name;

            id->symbol->access = AccessModifier::PUBLIC;

            this->exports.emplace_back(O_INCREF(id->value));
        } else {
            const auto &tuple = (ListExpression *) decl->name;
            for (const auto &cursor: tuple->elements) {
                const auto *id = (const Identifier *) cursor.get();

                id->symbol->access = AccessModifier::PUBLIC;

                this->exports.emplace_back(O_INCREF(id->value));
            }
        }
    }
}

void Parser::CheckSetImportAlias(HORString alias, Import *imp) const {
    if (!alias) {
        const auto *mod_name_end = ORSTRING_TO_CSTR(imp->path) + ORSTRING_LENGTH(imp->path);
        const auto length = ORSTRING_LENGTH(imp->path);

        unsigned int idx = 0;
        while (idx < length && std::isalnum(*((mod_name_end - idx) - 1)))
            idx++;

        if (!std::isalpha(*(mod_name_end - idx)))
            throw ParserException(85);

        alias = ORStringNew(this->isolate_, mod_name_end - idx, idx);
    }

    imp->alias = this->sym_t_->Declare(alias.get(), SymbolType::VARIABLE, imp->loc.start.offset);
    if (!imp->alias)
        throw SymbolTableException();

    imp->alias->flags |= SymbolFlags::CONST | SymbolFlags::INITIALIZED;
}

void Parser::ClassCheck(const Construct *clazz) const {
    auto *ct_block = (Block *) clazz->body;

    if (clazz->node_type != NodeType::CLASS)
        return;

    // Find init method in class body
    const Function *init = nullptr;

    for (const auto &stmt: ct_block->statements) {
        if (stmt->node_type == NodeType::INIT) {
            init = (Function *) stmt.get();

            break;
        }
    }

    if (init != nullptr && clazz->ext != nullptr) {
        // Check if super.init is the first statement in constructor
        const auto *init_body = (Block *) init->body;

        if (init_body->statements.empty())
            throw ParserException(80);

        const auto *call = (Call *) init_body->statements.front().get();
        const auto *stmt = (Selector *) call->left;

        if (call->node_type != NodeType::CALL
            || stmt->node_type != NodeType::SELECTOR
            || stmt->left->node_type != NodeType::IDENTIFIER
            || ((Identifier *) stmt->left)->kind != TokenType::SUPER
            || stmt->right->node_type != NodeType::IDENTIFIER
            || ORStringCompare(((Identifier *) stmt->right)->value, kInitMethodName) != 0
        )
            throw ParserException(80);
    }

    // Inject a synthetic constructor if no init method is defined
    if (init == nullptr)
        ct_block->statements.emplace_back(std::move(this->InjectInit(clazz->loc)));
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

        if (this->Match(TokenType::COMMENT_DOC, TokenType::COMMENT_MODULE)) {
            if (this->doc_.type == TokenType::COMMENT_DOC || this->doc_.type == TokenType::COMMENT_MODULE)
                this->doc_.~Token();

            this->doc_ = std::move(this->tkcur_);
        } else if (this->doc_.type == TokenType::COMMENT_DOC
                   && !this->Match(TokenType::DECORATOR, TokenType::KW_CLASS, TokenType::KW_NATIVE,
                                   TokenType::KW_TRAIT, TokenType::KW_FUNC, TokenType::SEMICOLON,
                                   TokenType::END_OF_LINE, TokenType::KW_PROT, TokenType::KW_PUB))
            this->doc_.~Token();
    } while (this->TokenInRange(TokenType::COMMENT_BEGIN, TokenType::COMMENT_END));
}

void Parser::EatNL() {
    if (this->tkcur_.type == TokenType::END_OF_LINE)
        this->Eat(true);
}

void Parser::IgnoreNewLineIF(TokenType type) {
    const Token *peek;

    if (this->tkcur_.type != TokenType::END_OF_LINE)
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
    auto module = MakeModule(this->isolate_, TKCUR_LOC);
    if (!module)
        return {};

    const auto r_module = module.get();

    if ((r_module->filename = ORStringNew(this->isolate_, this->filename_).release()) == nullptr)
        return {};

    if ((this->sym_t_ = SymbolTable::New(this->isolate_)) == nullptr)
        return {};

    try {
        Context isolate(this, ContextType::MODULE);
        Loc loc{};

        // LOAD FIRST COMMENT
        this->Eat(true);
        r_module->docstring = this->GetDocString(true).release();

        loc.start = TKCUR_START;

        while (!this->Match(TokenType::END_OF_FILE)) {
            r_module->statements.push_back(this->ParseStatement());

            loc.end = TKCUR_END;

            if (!this->Match(TokenType::END_OF_FILE, TokenType::END_OF_LINE, TokenType::SEMICOLON))
                throw ParserException(0);

            while (this->MatchEat(TokenType::SEMICOLON, true));
        }

        this->sym_t_->LeaveScope(loc.end.offset, loc.end.line);

        module->sym_t = this->sym_t_;
        this->sym_t_ = nullptr;

        module->exports = std::move(this->exports);

        return module;
    } catch (DatatypeException &) {
        this->error_.type = ParserErrorType::NOMEM;
    } catch (ParserException &e) {
        this->error_.type = ParserErrorType::SYNTAX;
        this->error_.message = kStandardError[e.err_idx];
        this->error_.token = std::move(this->tkcur_);
    } catch (ScannerException &) {
        this->error_.type = ParserErrorType::SYNTAX;

        if (this->scanner_.status == ScannerStatus::NOMEM)
            this->error_.type = ParserErrorType::NOMEM;

        this->error_.message = this->scanner_.GetStatusMessage();
    } catch (SymbolTableException &) {
        this->error_.type = ParserErrorType::GENERIC_ERROR;

        if (this->sym_t_->status == SymbolTableError::MEMORY_ERROR)
            this->error_.type = ParserErrorType::NOMEM;

        this->error_.message = this->sym_t_->GetStatusMessage();
        this->error_.token = std::move(this->tkcur_);
    }

    return {};
}

ParserError Parser::GetLastError() noexcept {
    return std::move(this->error_);
}
