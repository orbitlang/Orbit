// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/irbuilder.h>

using namespace liftoff;
using namespace liftoff::ir;

orbiter::OPCode InfixOp2OpCode(const scanner::TokenType tt, const bool imm, orbiter::ArithFlags &flags) {
    flags = orbiter::ArithFlags::NONE;

    switch (tt) {
        case scanner::TokenType::PLUS:
            return orbiter::OPCode::ADD;
        case scanner::TokenType::MINUS:
            return orbiter::OPCode::SUB;
        case scanner::TokenType::ASTERISK:
            return orbiter::OPCode::MUL;
        case scanner::TokenType::SLASH:
            return orbiter::OPCode::DIV;
        case scanner::TokenType::SLASH_SLASH:
            flags = orbiter::ArithFlags::FLOAT;
            return orbiter::OPCode::DIV;
        case scanner::TokenType::PERCENT:
            flags = orbiter::ArithFlags::DIV_REM;
            return orbiter::OPCode::DIV;

        case scanner::TokenType::AMPERSAND:
            return orbiter::OPCode::AND;
        case scanner::TokenType::PIPE:
            return orbiter::OPCode::OR;
        case scanner::TokenType::CARET:
            return orbiter::OPCode::XOR;

        case scanner::TokenType::SHL:
            return imm ? orbiter::OPCode::SHLI : orbiter::OPCode::SHLR;
        case scanner::TokenType::SHR:
            return imm ? orbiter::OPCode::SHRI : orbiter::OPCode::SHRR;

        default:
            assert(false); // Never get here
    }

    return {};
}

Instruction *IRBuilder::BinaryOP(const parser::Binary *binary) {
    const auto *nr = (parser::Binary *) binary->right;

    Instruction *left;
    Instruction *right = nullptr;

    bool left2right = true;
    bool r_ignore = false;

    if (nr->node_type == parser::NodeType::BINARY
        && (nr->token_type == scanner::TokenType::ASTERISK
            || nr->token_type == scanner::TokenType::SLASH
            || nr->token_type == scanner::TokenType::SLASH_SLASH)) {
        left2right = false;
    }

    if (binary->right->node_type == parser::NodeType::LITERAL
        && (binary->token_type == scanner::TokenType::SHL
            || binary->token_type == scanner::TokenType::SHR)) {
        const auto literal = (parser::Literal *) binary->right;

        if (O_IS_SMI(literal->literal)) {
            const auto number = ((PtrSize) literal->literal) >> 1;

            // Max immediate size: <= 0xFFFF
            if (number <= 0xFFFF) {
                right = (Instruction *) number;
                r_ignore = true;
            }
        }
    }

    if (left2right) {
        left = this->visit(binary->left);

        if (!r_ignore)
            right = this->visit(binary->right);
    } else {
        if (!r_ignore)
            right = this->visit(binary->right);

        left = this->visit(binary->left);
    }

    assert(right != nullptr);

    auto op_flags = orbiter::ArithFlags::NONE;

    auto op_code = InfixOp2OpCode(binary->token_type, r_ignore, op_flags);

    if (r_ignore)
        return this->builder_.CreateBinaryOpFlags(op_code, (U8) op_flags, left, (U16) ((PtrSize) right));

    return this->builder_.CreateBinaryOpFlags(op_code, (U8) op_flags, left, right);
}

Instruction *IRBuilder::CreateJumpForElvisOrNil(const parser::Binary *binary, orbiter::OPCode opcode) {
    auto *left = this->visit(binary->left);

    auto *end = this->builder_.CreateBasicBlock();

    this->builder_.CreateBranch(opcode, left, nullptr, end);

    auto *right = this->visit(binary->right);

    this->builder_.AppendBasicBlock(end);

    const auto phi = this->builder_.CreatePhi();

    return phi->AddTarget(left)->AddTarget(right);
}

Instruction *IRBuilder::LoadVariable(const Symbol *symbol) {
    Instruction *ret = this->builder_.context->GetLastActiveVariableLoad(symbol);
    auto offset = (I16) symbol->offset;

    if (ret != nullptr)
        return ret;

    if (symbol->upvalue) {
        ret = this->builder_.LoadFromClosureAtOffset(offset, symbol->defining_scope == this->sym_t_->scope
                                                                 ? orbiter::ClosureLSMode::STACK
                                                                 : orbiter::ClosureLSMode::FUNC_SLOT);

        goto EXIT;
    }

    if (symbol->type == SymbolType::UNKNOWN) {
        offset = (I16) (symbol->defining_scope->GetLocalVariableCount()
                        + this->builder_.context->PushUnknownProps(symbol->name));

        ret = this->builder_.LoadFromOffset(orbiter::OPCode::LDGBL, offset, 0);

        goto EXIT;
    }

    // TODO: Class/Trait
    if (symbol->defining_scope->type == ScopeType::MODULE) {
        if (this->level_ == OptimizationLevel::OFF && symbol->defining_scope != this->sym_t_->scope) {
            offset = (I16) (this->builder_.context->PushUnknownProps(symbol->name)
                            + this->sym_t_->scope->GetLocalVariableCount());

            ret = this->builder_.LoadFromOffset(orbiter::OPCode::LDGBL, offset, 0);
        } else
            ret = this->builder_.LoadFromOffset(orbiter::OPCode::LDGOFF, offset, 0);

        goto EXIT;
    }

    if (symbol->type == SymbolType::PARAMETER) {
        const auto params_count = (I16) this->sym_t_->scope->GetParameterCount();
        const auto p_offset = params_count - offset;

        assert(p_offset > 0);

        offset = (I16) -p_offset;
    }

    ret = this->builder_.LoadFromStackOffset(offset);

EXIT:
    this->builder_.context->current_->UseVar(symbol);

    this->builder_.context->AddActiveVar(symbol, ret);

    return ret;
}

Instruction *IRBuilder::StoreVariable(const Symbol *symbol, Instruction *value, bool decl) {
    auto offset = (I16) symbol->offset;

    this->builder_.context->InvalidateActiveVar(symbol);

    if (value == nullptr)
        value = this->builder_.LoadNilValue();

    if (symbol->upvalue) {
        this->builder_.StoreToClosureAtOffset(value, offset,
                                              symbol->defining_scope == this->sym_t_->scope
                                                  ? orbiter::ClosureLSMode::STACK
                                                  : orbiter::ClosureLSMode::FUNC_SLOT);

        goto EXIT;
    }

    if (symbol->type == SymbolType::UNKNOWN) {
        offset = (I16) (this->builder_.context->PushUnknownProps(symbol->name)
                        + this->sym_t_->scope->GetLocalVariableCount());

        this->builder_.CreateStoreVariable(orbiter::OPCode::STGBL, offset, 0, value);

        goto EXIT;
    }

    if (symbol->defining_scope->type == ScopeType::MODULE) {
        if (decl) {
            auto v_flags = orbiter::NewVariableFlags::VARIABLE;
            if (symbol->access == AccessModifier::PUBLIC)
                v_flags |= orbiter::NewVariableFlags::PUBLIC;

            this->builder_.context->PushKnownProps(symbol->name);

            this->builder_.CreateStoreVariable(orbiter::OPCode::NGBLV, offset, (U8) v_flags, value);

            goto EXIT;
        }

        if (this->level_ == OptimizationLevel::OFF && symbol->defining_scope != this->sym_t_->scope) {
            offset = (I16) (this->sym_t_->scope->GetLocalVariableCount() +
                            this->builder_.context->PushUnknownProps(symbol->name));

            this->builder_.CreateStoreVariable(orbiter::OPCode::STGBL, offset, 0, value);
        } else
            this->builder_.CreateStoreVariable(orbiter::OPCode::STGOFF, offset, 0, value);

        goto EXIT;
    }

    if (symbol->type == SymbolType::PARAMETER) {
        const auto params_count = (I16) this->sym_t_->scope->GetParameterCount();
        const auto p_offset = params_count - offset;

        assert(p_offset > 0);

        offset = (I16) -p_offset;
    }

    this->builder_.StoreToStackOffset(value, offset);

EXIT:
    this->builder_.context->current_->DefVar(symbol);

    return value;
}

Instruction *IRBuilder::visitASTNode(parser::ASTNode *node) {
    // TODO: Implement ASTNode visitation
    return nullptr;
}

Instruction *IRBuilder::visitAssignment(parser::Assignment *node) {
    Instruction *value = nullptr;
    const Symbol *sym;

    if (node->name->node_type == parser::NodeType::TUPLE) {
        //assert(false);
        return nullptr;
    }

    sym = ((parser::Identifier *) node->name)->symbol;

    if (node->value != nullptr)
        value = this->visit(node->value);

    return this->StoreVariable(sym, value, node->node_type == parser::NodeType::VAR_DECLARATION);
}

Instruction *IRBuilder::visitBinary(parser::Binary *node) {
    Instruction *ret = nullptr;
    Instruction *left;
    Instruction *right;

    const auto tk_type = node->token_type;
    auto flags = orbiter::MembershipFlags::IN;

    switch (node->node_type) {
        case parser::NodeType::BINARY:
            return this->BinaryOP(node);
        case parser::NodeType::CMPEQ: {
            left = this->visit(node->left);
            right = this->visit(node->right);

            if (tk_type == scanner::TokenType::EQUAL_EQUAL || tk_type == scanner::TokenType::NOT_EQUAL) {
                ret = this->builder_.CreateBinaryOpFlags(orbiter::OPCode::EQ,
                                                         (U8) orbiter::EqualityMode::NORMAL, left, right);
            }

            if (tk_type == scanner::TokenType::EQUAL_STRICT || tk_type == scanner::TokenType::NOT_EQUAL_STRICT) {
                ret = this->builder_.CreateBinaryOpFlags(orbiter::OPCode::EQ,
                                                         (U8) orbiter::EqualityMode::STRICT, left, right);
            }

            if (tk_type == scanner::TokenType::NOT_EQUAL || tk_type == scanner::TokenType::NOT_EQUAL_STRICT)
                return this->builder_.CreateUnaryOp(orbiter::OPCode::NOT, ret);

            if (tk_type == scanner::TokenType::LESS) {
                ret = this->builder_.CreateBinaryOpFlags(orbiter::OPCode::CMP,
                                                         (U8) orbiter::ComparisonMode::LT,
                                                         left, right);
            } else if (tk_type == scanner::TokenType::LESS_EQ) {
                ret = this->builder_.CreateBinaryOpFlags(orbiter::OPCode::CMP,
                                                         (U8) (orbiter::ComparisonMode::LT |
                                                               orbiter::ComparisonMode::EQ),
                                                         left, right);
            } else if (tk_type == scanner::TokenType::GREATER) {
                ret = this->builder_.CreateBinaryOpFlags(orbiter::OPCode::CMP,
                                                         (U8) orbiter::ComparisonMode::GT,
                                                         left, right);
            } else if (tk_type == scanner::TokenType::GREATER_EQ) {
                ret = this->builder_.CreateBinaryOpFlags(orbiter::OPCode::CMP,
                                                         (U8) (orbiter::ComparisonMode::GT |
                                                               orbiter::ComparisonMode::EQ),
                                                         left, right);
            }

            assert(ret != nullptr);

            return ret;
        }
        case parser::NodeType::ELVIS:
            return this->CreateJumpForElvisOrNil(node, orbiter::OPCode::JT);
        case parser::NodeType::NULL_COALESCING:
            return this->CreateJumpForElvisOrNil(node, orbiter::OPCode::JEN);
        case parser::NodeType::NOT_IN:
            flags = orbiter::MembershipFlags::NOT_IN;
        case parser::NodeType::IN:
            left = this->visit(node->left);
            right = this->visit(node->right);

            return this->builder_.CreateBinaryOpFlags(orbiter::OPCode::MEMB, (U8) flags, left, right);
        case parser::NodeType::SYNC_BLOCK: {
            left = this->visit(node->left);

            const JBlock jblock(&this->builder_, left);

            this->builder_.CreateUnaryOp(orbiter::OPCode::SYNC_ENTER, left);

            this->visit(node->right);

            this->builder_.CreateUnaryOp(orbiter::OPCode::SYNC_EXIT, left);
        }
        default:
            assert(false);
    }

    return nullptr;
}

Instruction *IRBuilder::visitBlock(const parser::Block *node) {
    Instruction *last = nullptr;

    for (auto &statement: node->statements)
        last = this->visit(statement.get());

    return last;
}

Instruction *IRBuilder::visitBranch(const parser::Branch *node) {
    BranchInstruction *last = nullptr;

    auto *end = this->builder_.CreateBasicBlock();
    auto *orelse = end;

    auto *value = this->visit(node->test);

    this->builder_.CreateBranch(orbiter::OPCode::JF, value, nullptr, orelse);

    this->sym_t_->EnterNestedScope(node->body->loc.start.offset);

    this->visit(node->body);

    this->sym_t_->LeaveNestedScope();

    if (node->orelse != nullptr) {
        last = (BranchInstruction *) this->builder_.CreateBranch(orbiter::OPCode::JMP, nullptr, orelse, nullptr);

        this->visit(node->orelse);
    }

    if (last != nullptr) {
        if (this->builder_.context->current_->IsInstructionListEmpty())
            last->SetBasicBlock(this->builder_.context->current_);
        else
            last->SetBasicBlock(this->builder_.CreateAppendBasicBlock());
    } else
        this->builder_.AppendBasicBlock(end);

    return nullptr;
}

Instruction *IRBuilder::visitCall(parser::Call *node) {
    for (const auto &arg: node->args) {
        auto *arg_value = this->visit(arg.get());
        this->builder_.StackPush(arg_value);
    }

    auto *func = this->visit(node->left);

    return this->builder_.CreateCall(func, node->args.size());
}

Instruction *IRBuilder::visitCatchBlock(parser::CatchBlock *node) {
    // TODO: Implement CatchBlock visitation
    return nullptr;
}

Instruction *IRBuilder::visitConstruct(parser::Construct *node) {
    // TODO: Implement Construct visitation
    return nullptr;
}

Instruction *IRBuilder::visitDecorator(parser::Decorator *node) {
    // TODO: Implement Decorator visitation
    return nullptr;
}

Instruction *IRBuilder::visitFunction(const parser::Function *node) {
    // params / ret addr / EBP / [locals] / [closure_ptr]

    auto f_flags = orbiter::LoadFuncFlags::SIMPLE;

    this->builder_.IRContextNew(IRContextType::FUNCTION);

    if (!this->sym_t_->EnterScope(node->name))
        throw SymbolTableException();

    this->builder_.context->stack_slot = this->sym_t_->scope->GetLocalVariableCount();

    // Alloc stack space for local variables
    this->builder_.AllocStackSlots(this->builder_.context->stack_slot, orbiter::AllocaFlags::DEFAULT);

    // Check if this function can create a lexical environment, if yes, allocate another slot in stack
    if (this->sym_t_->scope->ShouldCreateClosure()) {
        this->builder_.AllocStackSlots(1, orbiter::AllocaFlags::ZERO_INIT);

        const auto closure = this->builder_.CreateUnaryOp(orbiter::OPCode::CLONEW,
                                                          this->sym_t_->scope->GetClosureSize());

        this->builder_.StoreToStackOffset(closure, (I16) node->params.size());

        this->CaptureParametersIntoClosure(node);
    }

    if (this->sym_t_->scope->closure)
        f_flags = orbiter::LoadFuncFlags::CLOSURE;

    this->visit(node->body);

    this->sym_t_->LeaveScope();

    this->builder_.LeaveContext();

    auto *res = this->builder_.LoadCodeObject(this->builder_.context->GetSubcontextCount() - 1);

    auto *func = this->builder_.LoadFunction(res, f_flags);

    if (node->anon)
        return func;

    const auto *sym = this->sym_t_->Lookup(node->name, node->loc.start.offset);
    if (sym == nullptr)
        throw SymbolTableException();

    return this->StoreVariable(sym, func, true);
}

Instruction *IRBuilder::visitIdentifier(parser::Identifier *node) {
    const auto *sym = node->symbol;

    assert(sym != nullptr);

    return this->LoadVariable(sym);
}

Instruction *IRBuilder::visitImport(parser::Import *node) {
    // TODO: Implement Import visitation
    return nullptr;
}

Instruction *IRBuilder::visitImportName(parser::ImportName *node) {
    // TODO: Implement ImportName visitation
    return nullptr;
}

Instruction *IRBuilder::visitJump(const parser::Jump *node) {
    const auto b_target = this->builder_.context->j_chain->FindLabeledBlock(node->label);

    if (b_target == nullptr)
        assert(false); // TODO ERROR

    this->PutSyncExit(b_target);

    return this->builder_.CreateJump(
        node->token_type == scanner::TokenType::KW_BREAK ? b_target->end : b_target->begin);
}

Instruction *IRBuilder::visitLabel(const parser::Label *node) {
    JBlock _(&this->builder_, JBlockType::LABEL, node->label);

    return this->visit(node->statement);
}

Instruction *IRBuilder::visitListExpression(parser::ListExpression *node) {
    // TODO: Implement ListExpression visitation
    return nullptr;
}

Instruction *IRBuilder::visitLiteral(parser::Literal *node) {
    if (node->literal == orbiter::datatype::kOddBallNIL)
        return this->builder_.LoadNilValue();

    if (!O_IS_OBJECT(node->literal)) {
        if ((MSize) node->literal == orbiter::datatype::kOddBallFALSE)
            return this->builder_.LoadFalseValue();

        if ((MSize) node->literal == orbiter::datatype::kOddBallTRUE)
            return this->builder_.LoadTrueValue();

        return this->builder_.LoadImmediate((MachineSize) node->literal);
    }

    const auto offset = this->builder_.context->PushStaticValue(node->literal);
    return this->builder_.LoadConstant(offset);
}

Instruction *IRBuilder::visitLoop(const parser::Loop *node) {
    if (node->node_type == parser::NodeType::FOR_IN) {
        this->visitForInLoop(node);

        return nullptr;
    }

    const JBlock jb(&this->builder_, JBlockType::LOOP, nullptr);

    if (node->node_type == parser::NodeType::LOOP) {
        this->builder_.AppendBasicBlock(jb.begin);

        if (node->test != nullptr) {
            const auto test = this->visit(node->test);

            this->builder_.CreateBranch(orbiter::OPCode::JF, test, nullptr, jb.end);
        }

        this->sym_t_->EnterNestedScope(node->body->loc.start.offset);

        this->visit(node->body);

        this->builder_.CreateJump(jb.begin);

        this->sym_t_->LeaveNestedScope();

        this->builder_.AppendBasicBlock(jb.end);

        return nullptr;
    }

    if (node->node_type != parser::NodeType::FOR)
        assert(false); // Never get here

    this->sym_t_->EnterNestedScope(node->loc.start.offset);

    if (node->init != nullptr)
        this->visit(node->init);

    this->builder_.AppendBasicBlock(jb.begin);

    auto *test = this->visit(node->test);

    this->builder_.CreateBranch(orbiter::OPCode::JF, test, nullptr, jb.end);

    this->visit(node->body);

    if (node->inc != nullptr)
        this->visit(node->inc);

    this->builder_.CreateJump(jb.begin);

    this->sym_t_->LeaveNestedScope();

    this->builder_.AppendBasicBlock(jb.end);

    return nullptr;
}

Instruction *IRBuilder::visitModule(parser::Module *node) {
    // TODO: Implement Module visitation
    return nullptr;
}

Instruction *IRBuilder::visitNativeFunc(parser::NativeFunc *node) {
    // TODO: Implement NativeFunc visitation
    return nullptr;
}

Instruction *IRBuilder::visitNativeParameter(parser::NativeParameter *node) {
    // TODO: Implement NativeParameter visitation
    return nullptr;
}

Instruction *IRBuilder::visitNativeVariable(parser::NativeVariable *node) {
    // TODO: Implement NativeVariable visitation
    return nullptr;
}

Instruction *IRBuilder::visitParameter(parser::Parameter *node) {
    // TODO: Implement Parameter visitation
    return nullptr;
}

Instruction *IRBuilder::visitSubscript(parser::Subscript *node) {
    // TODO: Implement Subscript visitation
    return nullptr;
}

Instruction *IRBuilder::visitSwitchCase(parser::SwitchCase *node) {
    // TODO: Implement SwitchCase visitation
    return nullptr;
}

Instruction *IRBuilder::visitSwitchBlock(parser::SwitchBlock *node) {
    // TODO: Implement SwitchBlock visitation
    return nullptr;
}

Instruction *IRBuilder::visitTryBlock(parser::TryBlock *node) {
    // TODO: Implement TryBlock visitation
    return nullptr;
}

Instruction *IRBuilder::visitUnary(const parser::Unary *node) {
    // TODO: Implement Unary visitation
    auto *value = this->visit(node->value);

    if (node->node_type == parser::NodeType::UNARY) {
        switch (node->token_type) {
            case scanner::TokenType::EXCLAMATION:
                return this->builder_.CreateUnaryOp(orbiter::OPCode::NOT, value);
            case scanner::TokenType::MINUS:
                return this->builder_.CreateUnaryOp(orbiter::OPCode::NEG, value);
            case scanner::TokenType::PLUS:
                return value; // + Is a No-Op
            case scanner::TokenType::TILDE:
                return this->builder_.CreateUnaryOp(orbiter::OPCode::MVN, value);
            default:
                assert(false);
        }
    }

    if (node->node_type == parser::NodeType::PANIC)
        return this->builder_.CreateUnaryOp(orbiter::OPCode::PANIC, value);

    if (node->node_type == parser::NodeType::RETURN)
        return this->builder_.CreateReturn(value, false);

    if (node->node_type == parser::NodeType::YIELD)
        return this->builder_.CreateReturn(value, true);

    assert(false);

    return nullptr;
}

void IRBuilder::CaptureParametersIntoClosure(const parser::Function *node) {
    // Traverse the parameter list and, if a parameter is an UPVALUE, store it in the closure object.
    // This ensures the parameter is accessible in the closure's scope.

    for (const auto &cursor: node->params) {
        const auto *sym = this->sym_t_->Lookup(((parser::Parameter *) cursor.get())->id, cursor->loc.start.offset);

        assert(sym != nullptr);

        if (sym->type == SymbolType::PARAMETER && sym->upvalue) {
            const auto params_count = (I16) this->sym_t_->scope->GetParameterCount();
            const auto p_offset = params_count - sym->stack_offset;

            assert(p_offset > 0);

            const auto value = this->builder_.LoadFromStackOffset((I16) -p_offset);
            this->StoreVariable(sym, value, false);
        }
    }
}

void IRBuilder::visitForInLoop(const parser::Loop *node) {
    const JBlock jb(&this->builder_, JBlockType::FOR_IN, nullptr);

    assert(false); // TODO: IMPL THIS!

    this->sym_t_->EnterNestedScope(node->loc.start.offset);

    this->sym_t_->LeaveNestedScope();
}

void IRBuilder::PutSyncExit(const JBlock *block) {
    const auto *cursor = this->builder_.context->j_chain;

    while (cursor != block) {
        if (cursor->type == JBlockType::SYNC)
            this->builder_.CreateUnaryOp(orbiter::OPCode::SYNC_EXIT, cursor->value);

        cursor = cursor->prev;
    }
}

IRCHandle IRBuilder::Generate(parser::ASTHandle<parser::Module *> &module) noexcept {
    assert(this->isolate_ == module->isolate); // Security check.

    try {
        // Set symbol table
        this->sym_t_ = module->sym_t;

        // Create first context
        this->builder_.IRContextNew(IRContextType::MODULE);

        for (auto &statement: module->statements) {
            this->visit(statement.get());
        }

        this->builder_.LeaveContext();

        auto *context = this->builder_.context;

        this->builder_.context = nullptr;

        return IRCHandle(context);
    } catch (const SymbolTableException &) {
        assert(false);
    }

    return {};
}
