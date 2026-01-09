// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/errors.h>

#include <orbit/liftoff/parser/parser.h>

#include <orbit/liftoff/ir/irbuilder.h>

using namespace liftoff;
using namespace liftoff::ir;

orbiter::OPCode InfixOp2OpCode(const scanner::TokenType tt, const bool imm, U8 &flags) {
    flags = 0;

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
            flags = (U8) orbiter::DivFlags::FLOAT;
            return orbiter::OPCode::DIV;
        case scanner::TokenType::PERCENT:
            flags = (U8) orbiter::DivFlags::DIV_REM;
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

orbiter::native::NativeType NativeTypeFromTokenType(const scanner::TokenType type) {
    switch (type) {
        case scanner::TokenType::DT_BOOL:
            return orbiter::native::NativeType::BOOL;
        case scanner::TokenType::DT_BYTE:
            return orbiter::native::NativeType::BYTE;
        case scanner::TokenType::DT_I8:
            return orbiter::native::NativeType::I8;
        case scanner::TokenType::DT_I16:
            return orbiter::native::NativeType::I16;
        case scanner::TokenType::DT_I32:
            return orbiter::native::NativeType::I32;
        case scanner::TokenType::DT_I64:
            return orbiter::native::NativeType::I64;
        case scanner::TokenType::DT_ISIZE:
            return orbiter::native::NativeType::ISIZE;
        case scanner::TokenType::DT_U8:
            return orbiter::native::NativeType::U8;
        case scanner::TokenType::DT_U16:
            return orbiter::native::NativeType::U16;
        case scanner::TokenType::DT_U32:
            return orbiter::native::NativeType::U32;
        case scanner::TokenType::DT_U64:
            return orbiter::native::NativeType::U64;
        case scanner::TokenType::DT_USIZE:
            return orbiter::native::NativeType::USIZE;
        case scanner::TokenType::DT_OPAQUE:
        case scanner::TokenType::DT_PTR:
            return orbiter::native::NativeType::PTR;
        case scanner::TokenType::DT_F32:
            return orbiter::native::NativeType::F32;
        case scanner::TokenType::DT_F64:
            return orbiter::native::NativeType::F64;
        default:
            assert(false); // Never get here
    }
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

    auto op_flags = (U8) 0;

    const auto op_code = InfixOp2OpCode(binary->token_type, r_ignore, op_flags);

    if (r_ignore)
        return this->builder_.CreateBinaryOpFlags(op_code, op_flags, left, (U16) ((PtrSize) right));

    return this->builder_.CreateBinaryOpFlags(op_code, op_flags, left, right);
}

Instruction *IRBuilder::CreateCall(const parser::Call *node, Instruction *f_src) {
    auto opcode = orbiter::OPCode::CALL;
    auto mode = orbiter::CallMode::FASTCALL;

    Instruction *arg_value = nullptr;
    Instruction *rest = nullptr;
    Instruction *kwargs = nullptr;

    if (node->node_type == parser::NodeType::DEFER) {
        if (this->builder_.context->deferred == 0)
            this->builder_.context->deferred = 1;

        opcode = orbiter::OPCode::DEFER;
    }

    // Args
    for (const auto &arg: node->args) {
        arg_value = this->visit(arg.get());

        this->builder_.StackPush(arg_value);
    }

    // NArgs
    Instruction *nargs = nullptr;
    for (const auto &narg: node->nargs) {
        const auto r_narg = (parser::Parameter *) narg.get();

        if (nargs == nullptr)
            nargs = this->builder_.CreateUnaryOp(orbiter::OPCode::NDICT, node->nargs.size());

        const auto key_offset = this->builder_.context->PushStaticValue((orbiter::datatype::OObject *) r_narg->id);

        const auto key = this->builder_.LoadConstant(key_offset);
        const auto value = this->visit(r_narg->value);

        this->builder_.CreateManip(orbiter::OPCode::ADDELEM, nargs, key, value);
    }

    if (nargs != nullptr)
        mode |= orbiter::CallMode::NARGS;

    if (node->rest != nullptr) {
        rest = this->visit(((parser::Unary *) node->rest)->value);

        mode |= orbiter::CallMode::REST_ARG;
    }

    if (node->kwargs != nullptr) {
        kwargs = this->visit(((parser::Unary *) node->kwargs)->value);

        mode |= orbiter::CallMode::KW_ARG;
    }

    // Cleanup flags
    if ((int) mode > 1)
        mode &= ~orbiter::CallMode::FASTCALL;

    const auto call = (CallInstr *) this->builder_.CreateCallDetached(opcode, f_src, node->args.size(), mode);

    if (nargs != nullptr)
        call->SetNargs(nargs);

    if (rest != nullptr)
        call->SetRest(rest);

    if (kwargs != nullptr)
        call->SetKwargs(kwargs);

    if (opcode == orbiter::OPCode::DEFER)
        this->builder_.context->deferred_stack_count += node->args.size();

    return call;
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

Instruction *IRBuilder::LoadParameter(const Symbol *symbol) {
    assert(symbol->type == SymbolType::PARAMETER);

    const auto params_count = (I16) this->sym_t_->scope->GetParameterCount();
    const auto sym_offset = (I16) ENUMBITMASK_ISTRUE(symbol->flags, SymbolFlags::UPVALUE)
                                ? symbol->stack_offset
                                : symbol->offset;
    const auto p_offset = (params_count - sym_offset) + kStackPrologueOffset;

    assert(p_offset > 0);

    return this->builder_.LoadFromStackOffset(kBaseStackPointerReg, (I16) -p_offset, false);
}

Instruction *IRBuilder::LoadSelfParam(MSize offset) {
    const auto *self = this->sym_t_->Lookup("self", offset);
    if (self == nullptr)
        throw SymbolTableException();

    return this->LoadParameter(self);
}

Instruction *IRBuilder::LoadVariable(const Symbol *symbol) {
    Instruction *ret = this->builder_.context->GetLastActiveVariableLoad(symbol);
    if (ret != nullptr)
        return ret;

    auto offset = (I16) symbol->offset;

    // *** UNKNOWN ***
    if (symbol->type == SymbolType::UNKNOWN) {
        offset = (I16) this->builder_.context->PushUnknownProps(symbol->name);
        ret = this->builder_.LoadFromOffset(orbiter::OPCode::LDGBL, 0, offset, 0);

        goto EXIT;
    }

    // *** CATCH Variable ***
    if (symbol->type == SymbolType::ECONST) {
        ret = this->builder_.CreateUnaryOp(orbiter::OPCode::LDEXC);

        goto EXIT;
    }

    // *** MODULE ***
    if (symbol->defining_scope->type == ScopeType::MODULE) {
        if (!this->is_module_) {
            offset = (I16) this->builder_.context->PushUnknownProps(symbol->name);
            ret = this->builder_.LoadFromOffset(orbiter::OPCode::LDGBL, 0, offset, 0);
        } else
            ret = this->builder_.LoadFromOffset(orbiter::OPCode::LDGOFF, 0, offset, 0);

        goto EXIT;
    }

    // *** CLASS / TRAIT ***
    // TODO

    // *** CLOSURE ***
    if (ENUMBITMASK_ISTRUE(symbol->flags, SymbolFlags::UPVALUE)) {
        ret = this->builder_.LoadFromOffset(orbiter::OPCode::CLOLDR, 0, offset, 0);

        goto EXIT;
    }

    // *** PARAMETERS ***
    if (symbol->type == SymbolType::PARAMETER) {
        ret = this->LoadParameter(symbol);

        goto EXIT;
    }

    ret = this->builder_.LoadFromStackOffset(kBaseStackPointerReg, offset, false);

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

    auto v_flags = orbiter::VariableFlags::VARIABLE;

    if (symbol->type == SymbolType::CONSTANT || symbol->type == SymbolType::NATIVE_CONST)
        v_flags = orbiter::VariableFlags::CONSTANT;

    if (symbol->access == AccessModifier::PUBLIC)
        v_flags |= orbiter::VariableFlags::PUBLIC;
    else if (symbol->access == AccessModifier::PROTECTED)
        v_flags |= orbiter::VariableFlags::PROTECTED;

    // *** UNKNOWN ***
    if (symbol->type == SymbolType::UNKNOWN) {
        offset = (I16) this->builder_.context->PushUnknownProps(symbol->name);
        this->builder_.CreateStoreVariable(orbiter::OPCode::STGBL, offset, 0, value);

        goto EXIT;
    }

    // *** MODULE ***
    if (symbol->defining_scope->type == ScopeType::MODULE) {
        if (decl && !this->is_module_) {
            offset = (I16) this->builder_.context->PushUnknownProps(symbol->name);
            this->builder_.CreateStoreVariable(orbiter::OPCode::NGBLV, offset, (U8) v_flags, value);

            goto EXIT;
        }

        if (!this->is_module_) {
            offset = (I16) this->builder_.context->PushUnknownProps(symbol->name);
            this->builder_.CreateStoreVariable(orbiter::OPCode::STGBL, offset, 0, value);
        } else {
            if (decl && (symbol->access == AccessModifier::PUBLIC || symbol->access == AccessModifier::PROTECTED))
                this->builder_.context->ExportSymbol(symbol, v_flags);

            this->builder_.CreateStoreVariable(orbiter::OPCode::STGOFF, offset, 0, value);
        }

        goto EXIT;
    }

    // *** CLASS / TRAIT ***
    if (symbol->defining_scope->type == ScopeType::CLASS || symbol->defining_scope->type == ScopeType::TRAIT) {
        // For constructs (Class/Trait) variables must be handled as constants,
        // since variables are not managed this way. (Yes, functions and methods are treated as constants)
        v_flags |= orbiter::VariableFlags::CONSTANT;

        if (symbol->type != SymbolType::CONSTANT && symbol->defining_scope->type == ScopeType::CLASS) {
            v_flags |= orbiter::VariableFlags::CP_INLINE;

            this->builder_.context->local_slots += 1;
        }

        this->builder_.context->ExportSymbol(symbol, v_flags);

        offset = (I16) this->builder_.context->PushUnknownProps(symbol->name);
        this->builder_.CreateManipType(orbiter::OPCode::SETPROP, this->ct_active_->tp_ptr, value, offset);

        return value;
    }

    // *** CLOSURE ***
    if (ENUMBITMASK_ISTRUE(symbol->flags, SymbolFlags::UPVALUE)) {
        this->builder_.StoreToClosureAtOffset(value, offset);

        goto EXIT;
    }

    // *** PARAMETERS ***
    if (symbol->type == SymbolType::PARAMETER) {
        const auto params_count = (I16) this->sym_t_->scope->GetParameterCount();
        const auto p_offset = (params_count - offset) + kStackPrologueOffset;

        assert(p_offset > 0);

        offset = (I16) -p_offset;
    }

    this->builder_.StoreToStackOffset(value, kBaseStackPointerReg, offset);

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

    if (node->name->node_type == parser::NodeType::INDEX) {
        auto *last_op = this->visitSubscript((parser::Subscript *) node->name);

        value = this->visit(node->value);

        value = this->builder_.CreateIndexStore((SubscrInstruction *) last_op, value);

        this->builder_.context->DeleteInstruction(last_op);

        return value;
    }

    if (node->name->node_type == parser::NodeType::SLICE) {
        auto *last_op = this->visitSubscript((parser::Subscript *) node->name);

        value = this->visit(node->value);

        value = this->builder_.CreateSubscrStore((SubscrInstruction *) last_op, value);

        this->builder_.context->DeleteInstruction(last_op);

        return value;
    }

    if (node->name->node_type == parser::NodeType::SELECTOR) {
        const auto *selector = (parser::Selector *) node->name;
        const auto *property = ((parser::Identifier *) selector->right);

        this->visit(node->name);

        value = this->visit(node->value);

        // Replace last LDOBJP with STOBJP
        auto *ld = (LSObjectProp *) this->builder_.context->RFindFirstInstruction(orbiter::OPCode::LDOBJP);

        // Const check
        const auto *sym = this->sym_t_->Lookup(property->value, property->loc.start.offset, true);
        if (sym != nullptr && (sym->type == SymbolType::CONSTANT
                               || sym->type == SymbolType::FUNC
                               || sym->type == SymbolType::METHOD))
            assert(false); // TODO: Constant error

        auto *obj = (Instruction *) ld->operands[0].value;

        assert(ld!=nullptr);

        auto *st = this->builder_.GetStoreObjectProp(obj, value, ld->offset,
                                                     ld->flags == orbiter::LoadObjectPropFlags::KEY);

        this->builder_.context->DeleteInstruction(ld);

        this->builder_.AddInstruction(st);

        return st;
    }

    if (node->name->node_type == parser::NodeType::TUPLE) {
        assert(false);

        return nullptr;
    }

    const auto *sym = ((parser::Identifier *) node->name)->symbol;

    if (sym->defining_scope->type == ScopeType::CLASS || sym->defining_scope->type == ScopeType::TRAIT) {
        if (sym->type == SymbolType::CONSTANT) {
            value = this->visit(node->value);

            return this->StoreVariable(sym, value, node->node_type == parser::NodeType::VAR_DECLARATION);
        }

        if (sym->type == SymbolType::VARIABLE && sym->defining_scope->type != ScopeType::TRAIT) {
            auto v_flags = orbiter::VariableFlags::VARIABLE;

            if (sym->access == AccessModifier::PUBLIC)
                v_flags |= orbiter::VariableFlags::PUBLIC;
            else if (sym->access == AccessModifier::PROTECTED)
                v_flags |= orbiter::VariableFlags::PROTECTED;

            this->builder_.context->ExportSymbol(sym, v_flags);

            this->ct_active_->properties.emplace_back(node);

            this->builder_.context->local_slots += 1;

            return value;
        }

        assert(false);
    }

    if (sym->type == SymbolType::CONSTANT)
        assert(false); // TODO: Constant error

    if (node->value != nullptr)
        value = this->visit(node->value);

    return this->StoreVariable(sym, value, node->node_type == parser::NodeType::VAR_DECLARATION);
}

Instruction *IRBuilder::visitBinary(parser::Binary *node) {
    Instruction *left;
    Instruction *right;

    const auto tk_type = node->token_type;
    auto flags = orbiter::MembershipFlags::IN;

    switch (node->node_type) {
        case parser::NodeType::BINARY:
            return this->BinaryOP(node);
        case parser::NodeType::CMPEQ: {
            Instruction *ret = nullptr;

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

            const JBlock _(&this->builder_, left);

            this->builder_.CreateUnaryOp(orbiter::OPCode::SYNC_ENTER, left);

            this->visit(node->right);

            this->builder_.CreateUnaryOp(orbiter::OPCode::SYNC_EXIT, left);

            return nullptr;
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

Instruction *IRBuilder::visitCall(const parser::Call *node) {
    return this->visitCallPrepend(node, nullptr);
}

Instruction *IRBuilder::visitCallPrepend(const parser::Call *node, Instruction *p_arg) {
    const auto p_count = p_arg != nullptr ? 1 : 0;

    auto *func = this->visit(node->left);

    if (node->left->node_type == parser::NodeType::SELECTOR) {
        auto *obj = (Instruction *) func->operands[0].value;

        assert(obj->type() == ObjectType::INSTRUCTION);

        this->builder_.StackPushIF(obj, func, nullptr, orbiter::PushIfFlags::METHOD);

        if (p_arg != nullptr)
            this->builder_.StackPush(p_arg);

        auto *call = (CallInstr *) this->CreateCall(node, func);

        call->arguments += p_count + 1;

        call->mode |= orbiter::CallMode::METHOD;

        this->builder_.AddInstruction(call);

        if (node->node_type == parser::NodeType::DEFER)
            this->builder_.context->deferred_stack_count += p_count + 1;
        else
            this->builder_.context->stack_push_count -= call->arguments;

        return call;
    }

    if (p_arg != nullptr)
        this->builder_.StackPush(p_arg);

    auto *call = (CallInstr *) this->CreateCall(node, func);

    call->arguments += p_count;

    this->builder_.AddInstruction(call);

    if (node->node_type == parser::NodeType::DEFER)
        this->builder_.context->deferred_stack_count += p_count;
    else
        this->builder_.context->stack_push_count -= call->arguments;

    return call;
}

Instruction *IRBuilder::visitCatchBlock(parser::CatchBlock *node) {
    // TODO: Implement CatchBlock visitation
    return nullptr;
}

Instruction *IRBuilder::visitConstruct(const parser::Construct *node) {
    if (!this->sym_t_->EnterScope(node->name))
        throw SymbolTableException();

    const auto ctx = this->sym_t_->scope->type == ScopeType::CLASS ? IRContextType::CLASS : IRContextType::TRAIT;

    this->builder_.IRContextNew(ctx, 0);

    if (node->ext)
        this->builder_.StackPush(this->visit(node->ext));

    if (!node->impl.empty()) {
        for (auto &impl: node->impl)
            this->builder_.StackPush(this->visit(impl.get()));
    }

    if (ctx == IRContextType::CLASS) {
        const auto clazz = this->builder_.CreateUnaryOp(orbiter::OPCode::MKCLZ,
                                                        node->impl.size(),
                                                        node->ext != nullptr
                                                            ? (U16) orbiter::ClassFlags::EXTEND
                                                            : 0);

        this->builder_.StackDiscard((node->ext == nullptr ? 0 : 1) + node->impl.size());

        CTContext context(this, clazz);

        if (node->ext)
            context.extends_type = true;

        this->visit(node->body);

        this->builder_.CreateReturnSub(clazz);
    } else {
        const auto trait = this->builder_.CreateUnaryOp(orbiter::OPCode::MKTRT, node->impl.size());

        this->builder_.StackDiscard(node->impl.size());

        CTContext _(this, trait);

        this->visit(node->body);

        this->builder_.CreateReturnSub(trait);
    }

    this->builder_.context->name = orbiter::datatype::HORString(node->name);
    this->builder_.context->doc = orbiter::datatype::HORString(node->doc);

    this->builder_.LeaveContext();

    this->sym_t_->LeaveScope();

    auto *value = this->builder_.LoadExecLastCodeObject();

    const auto *sym = this->sym_t_->Lookup(node->name, node->loc.start.offset);
    if (sym == nullptr)
        throw SymbolTableException();

    return this->StoreVariable(sym, value, true);
}

Instruction *IRBuilder::visitDecorator(parser::Decorator *node) {
    auto *decorate = (parser::Function *) node->func;
    const auto back_a_flag = decorate->anon;

    decorate->anon = true;

    auto *first_param = this->visitFunction(decorate);

    decorate->anon = back_a_flag;

    for (auto &dec: node->decorators) {
        const auto *fdec = (parser::Call *) dec.get();

        first_param = this->visitCallPrepend(fdec, first_param);
    }

    // Store value here
    const auto *sym = this->sym_t_->Lookup(decorate->name, decorate->loc.start.offset);
    if (sym == nullptr)
        throw SymbolTableException();

    return this->StoreVariable(sym, first_param, true);
}

Instruction *IRBuilder::visitFunction(const parser::Function *node) {
    // -> params -> EBP -> RET ADDR -> [locals] -> [closure_ptr]

    Instruction *def_args = nullptr;

    auto f_flags = orbiter::LoadFuncFlags::SIMPLE;
    if (node->async)
        f_flags |= orbiter::LoadFuncFlags::ASYNC;

    if (node->method)
        f_flags |= orbiter::LoadFuncFlags::METHOD;

    if (node->node_type == parser::NodeType::INIT)
        f_flags |= orbiter::LoadFuncFlags::INIT;

    const auto params_count = this->ProcessFunctionParams(node, def_args, f_flags);

    if (!this->sym_t_->EnterScope(node->name))
        throw SymbolTableException();

    this->builder_.IRContextNew(IRContextType::FUNCTION, params_count);

    // *****************************************************************************************************************
    // ALLOC STACK SPACE
    // *****************************************************************************************************************
    const auto local_vars_count = this->sym_t_->scope->GetLocalVariableCount();

    // Alloc extra stack space for Closure object (IF ANY)
    auto alloc_size = local_vars_count;
    if (this->sym_t_->scope->closure)
        alloc_size += 1;

    if (alloc_size > 0)
        this->builder_.AllocStackSlots(alloc_size, orbiter::AllocaFlags::ZERO_INIT);

    this->builder_.context->vars_count = local_vars_count;

    // *****************************************************************************************************************
    // EOL
    // *****************************************************************************************************************

    // Load default value for constructor
    if (node->node_type == parser::NodeType::INIT) {
        Instruction *self = nullptr;
        Instruction *load_nil = nullptr;
        Instruction *value = nullptr;

        // Load super
        if (this->ct_active_->extends_type && ENUMBITMASK_ISTRUE(node->symbol->flags, SymbolFlags::SYNTETIC)) {
            const auto offset = (I16) this->builder_.context->PushUnknownProps(parser::kInitMethodName);

            self = this->LoadSelfParam(node->loc.end.offset);

            auto *s_init = this->builder_.LoadObjectProp(self, offset, true, true);

            this->builder_.StackPush(self);

            auto *call = this->builder_.CreateCallDetached(orbiter::OPCode::CALL, s_init, 1, orbiter::CallMode::METHOD);

            this->builder_.AddInstruction(call);
        }

        for (const auto &var: this->ct_active_->properties) {
            const auto *sym = ((parser::Identifier *) var->name)->symbol;

            if (var->value == nullptr) {
                if (load_nil == nullptr)
                    load_nil = this->builder_.LoadNilValue();

                value = load_nil;
            } else
                value = this->visit(var->value);

            if (self == nullptr)
                self = this->LoadSelfParam(node->loc.end.offset);

            this->builder_.StoreObjectProp(self, value, sym->offset, false);
        }
    }

    // Check if this function can create a lexical environment, if yes, allocate another slot in stack
    if (this->sym_t_->scope->ShouldCreateClosure()) {
        this->builder_.AllocStackSlots(1, orbiter::AllocaFlags::ZERO_INIT);

        const auto closure = this->builder_.CreateUnaryOp(orbiter::OPCode::CLONEW,
                                                          this->sym_t_->scope->GetClosureSize());

        this->builder_.StoreToStackOffset(closure, kBaseStackPointerReg, (I16) local_vars_count);

        this->CaptureParametersIntoClosure(node);
    }

    // Load closure from Function object
    if (this->sym_t_->scope->closure)
        this->builder_.LoadClosureObject(kBaseStackPointerReg, (I16) local_vars_count);

    const auto cleanup_count = node->params.size();

    if (node->body == nullptr)
        this->BuildMethodEmptyBody(node);
    else
        this->visit(node->body);

    if (this->builder_.CheckIfLastInstructionIs(orbiter::OPCode::RET))
        ((ReturnInstruction *) this->builder_.context->current_->instr.tail)->slots = cleanup_count;
    else
        this->builder_.CreateReturn(cleanup_count);

    this->builder_.LeaveContext();

    this->sym_t_->LeaveScope();

    auto *func = this->builder_.LoadFunction(this->builder_.LoadLastCodeObject(), def_args, f_flags);

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

Instruction *IRBuilder::visitImport(const parser::Import *node) {
    auto *module = this->builder_.LoadModule(node->path);

    if (node->node_type == parser::NodeType::IMPORT) {
        this->StoreVariable(node->alias, module, true);

        return module;
    }

    for (auto &name: node->names) {
        const auto *iname = (parser::ImportName *) name.get();

        const auto offset = (I16) this->builder_.context->PushUnknownProps(iname->name);

        auto *prop = this->builder_.LoadObjectProp(module, offset, true, false);

        this->StoreVariable(iname->alias, prop, true);
    }

    return module;
}

Instruction *IRBuilder::visitImportName(parser::ImportName *node) {
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
    const auto elements = node->elements.size();

    Instruction *container = nullptr;

    if (node->node_type == parser::NodeType::DICT) {
        assert(elements % 2 == 0);

        container = this->builder_.CreateUnaryOp(orbiter::OPCode::NDICT, elements / 2);

        for (auto i = 1; i < elements; i += 2) {
            auto *key = this->visit(node->elements[i - 1].get());
            auto *value = this->visit(node->elements[i].get());

            this->builder_.CreateManip(orbiter::OPCode::ADDELEM, container, key, value);
        }

        return container;
    }

    if (node->node_type == parser::NodeType::LIST)
        container = this->builder_.CreateUnaryOp(orbiter::OPCode::NLIST, elements);
    else if (node->node_type == parser::NodeType::TUPLE)
        container = this->builder_.CreateUnaryOp(orbiter::OPCode::NTUPLE, elements);

    for (auto &element: node->elements) {
        auto *value = this->visit(element.get());

        this->builder_.CreateManip(orbiter::OPCode::ADDELEM, container, value);
    }

    return container;
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

    return this->builder_.LoadConstant(node->literal);
}

Instruction *IRBuilder::visitLoop(const parser::Loop *node) {
    if (node->node_type == parser::NodeType::FOR_IN) {
        this->VisitForInLoop(node);

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

Instruction *IRBuilder::visitNativeFunc(const parser::NativeFunc *node) {
    auto &bind = this->builder_.context->native_bindings.emplace_back();
    const auto fid = this->builder_.context->native_bindings.size() - 1;

    bind.symbol = orbiter::datatype::HORString(node->native_name);
    bind.name = orbiter::datatype::HORString(node->alias->name);
    bind.library = orbiter::datatype::HORString(node->mod_name);

    // Parse params
    for (auto &native_param: node->parameters) {
        const auto param = (parser::NativeParameter *) native_param.get();
        bind.params.emplace_back(param->name, NativeTypeFromTokenType(param->kind));
    }

    bind.ret_type = NativeTypeFromTokenType(node->ret_type);
    bind.binding_type = orbiter::datatype::NativeBindingType::FUNC;

    auto *nt = this->builder_.CreateUnaryOp(orbiter::OPCode::LDNAT, fid, 0);
    return this->StoreVariable(node->alias, nt, true);;
}

Instruction *IRBuilder::visitNativeParameter(parser::NativeParameter *node) {
    return nullptr;
}

Instruction *IRBuilder::visitNativeVariable(const parser::NativeVariable *node) {
    auto &bind = this->builder_.context->native_bindings.emplace_back();
    const auto fid = this->builder_.context->native_bindings.size() - 1;

    bind.symbol = orbiter::datatype::HORString(node->native_name);
    bind.name = orbiter::datatype::HORString(node->alias->name);
    bind.library = orbiter::datatype::HORString(node->mod_name);
    bind.ret_type = NativeTypeFromTokenType(node->kind);
    bind.binding_type = node->alias->type == SymbolType::NATIVE_CONST
                            ? orbiter::datatype::NativeBindingType::CONST
                            : orbiter::datatype::NativeBindingType::VAR;

    auto *nt = this->builder_.CreateUnaryOp(orbiter::OPCode::LDNAT, fid, 0);
    return this->StoreVariable(node->alias, nt, true);;
}

Instruction *IRBuilder::visitNew(const parser::Unary *node) {
    const auto *func = (parser::Call *) node->value;

    auto *clazz = this->visit(func->left);

    auto *ctor = this->builder_.CreateUnaryOp(orbiter::OPCode::LDINIT, clazz);

    auto *self = this->builder_.CreateUnaryOp(orbiter::OPCode::NOBJ, clazz);

    this->builder_.StackPush(self);

    auto *call = (CallInstr *) this->CreateCall(func, ctor);

    call->mode |= orbiter::CallMode::METHOD;
    call->arguments += 1;

    this->builder_.AddInstruction(call);

    this->builder_.context->stack_push_count -= call->arguments;

    return self;
}

Instruction *IRBuilder::visitNilSafety(const parser::Unary *node) {
    auto *end = this->builder_.CreateBasicBlock();
    auto *nil_block = this->builder_.CreateBasicBlock();

    JBlock ctx(&this->builder_, JBlockType::NIL_SAFE);
    ctx.alt = nil_block;
    ctx.end = end;

    auto *value = this->visit(node->value);

    this->builder_.CreateJump(end);

    this->builder_.AppendBasicBlock(nil_block);

    auto *nil_val = this->builder_.LoadNilValue();

    this->builder_.AppendBasicBlock(end);

    auto *phy = this->builder_.CreatePhi();
    return phy->AddTarget(value)->AddTarget(nil_val);
}

Instruction *IRBuilder::visitParameter(parser::Parameter *node) {
    // TODO: Implement Parameter visitation
    return nullptr;
}

Instruction *IRBuilder::visitReturn(const parser::Unary *unary) {
    auto *value = unary->value != nullptr ? this->visit(unary->value) : this->builder_.LoadNilValue();
    auto pops_slot = 0;

    if (this->sym_t_->scope->type == ScopeType::FUNCTION || this->sym_t_->scope->type == ScopeType::GENERATOR)
        pops_slot = this->sym_t_->scope->GetParameterCount();

    this->PutSyncExit(nullptr);

    return this->builder_.CreateReturn(value, pops_slot);
}

Instruction *IRBuilder::visitSelector(parser::Selector *node) {
    auto *base = this->visit(node->left);

    const auto *key = (parser::Identifier *) node->right;

    bool super = false;

    assert(key->node_type == parser::NodeType::IDENTIFIER);

    if (node->token_type == scanner::TokenType::QUESTION_DOT) {
        auto *ctx = this->builder_.context->GetActiveContextIf(JBlockType::NIL_SAFE);
        if (ctx != nullptr)
            this->builder_.CreateBranch(orbiter::OPCode::JEN, base, nullptr, ctx->alt);
    }

    if (node->left->node_type == parser::NodeType::IDENTIFIER) {
        const auto *obj_base = (parser::Identifier *) node->left;

        if (obj_base->kind == scanner::TokenType::SELF
            && this->ct_active_ != nullptr
            && ((PhysInstruction *) this->ct_active_->tp_ptr)->opcode == orbiter::OPCode::MKCLZ) {
            const auto *sym = this->sym_t_->Lookup(key->value, key->loc.start.offset, true);

            if (sym != nullptr && sym->type != SymbolType::CONSTANT)
                return this->builder_.LoadObjectProp(base, sym->offset, false, false);
        }

        if (obj_base->kind == scanner::TokenType::SUPER)
            super = true;
    }

    const auto offset = (I16) this->builder_.context->PushUnknownProps(key->value);

    return this->builder_.LoadObjectProp(base, offset, true, super);
}

Instruction *IRBuilder::visitSubscript(const parser::Subscript *node) {
    const auto obj = this->visit(node->expression);
    Instruction *start = nullptr;

    if (node->node_type == parser::NodeType::INDEX) {
        start = this->visit(node->start);

        return this->builder_.CreateIndexLoad(obj, start);
    }

    Instruction *stop = nullptr;
    Instruction *step = nullptr;

    if (node->start != nullptr)
        start = this->visit(node->start);
    else
        start = this->builder_.LoadImmediate(0);

    if (node->stop != nullptr)
        stop = this->visit(node->stop);
    else
        stop = this->builder_.LoadNilValue();

    if (node->step != nullptr)
        step = this->visit(node->step);
    else
        step = this->builder_.LoadImmediate(1);

    return this->builder_.CreateSubscrLoad(obj, start, stop, step);
}

Instruction *IRBuilder::visitSwitchCase(const parser::SwitchCase *node) {
    return nullptr;
}

Instruction *IRBuilder::visitSwitchBlock(const parser::SwitchBlock *node) {
    Instruction *swtest = nullptr;
    if (node->test != nullptr)
        swtest = this->visit(node->test);

    BasicBlock *default_block = nullptr;
    const auto end_block = this->builder_.CreateBasicBlock();

    JBlock ctx{&this->builder_, JBlockType::SWITCH};
    ctx.end = end_block;

    std::vector<BasicBlock *> bodies;
    for (auto &case_: node->cases) {
        const auto *case_node = (parser::SwitchCase *) case_.get();

        bodies.push_back(this->builder_.CreateBasicBlock());

        if (case_node->tests.empty())
            default_block = bodies.back();

        for (auto &test: case_node->tests) {
            auto *case_test = this->visit(test.get());

            if (swtest == nullptr) {
                this->builder_.CreateBranch(orbiter::OPCode::JT, case_test, nullptr, bodies.back());

                continue;
            }

            auto *cmp = this->builder_.CreateBinaryOpFlags(orbiter::OPCode::EQ,
                                                           (U8) orbiter::EqualityMode::NORMAL,
                                                           swtest,
                                                           case_test);

            this->builder_.CreateBranch(orbiter::OPCode::JT, cmp, nullptr, bodies.back());
        }
    }

    // Jump to default or end of the switch statement
    this->builder_.CreateJump(default_block != nullptr ? default_block : end_block);

    // Process cases body
    auto case_index = 0;
    for (auto &case_: node->cases) {
        const auto *case_node = (parser::SwitchCase *) case_.get();

        this->builder_.AppendBasicBlock(bodies[case_index++]);

        this->sym_t_->EnterNestedScope(case_node->body->loc.start.offset);

        this->visit(case_node->body);

        this->sym_t_->LeaveNestedScope();

        if (!case_node->fallthrough)
            this->builder_.CreateJump(end_block);
    }

    this->builder_.AppendBasicBlock(end_block);

    return nullptr;
}

Instruction *IRBuilder::visitTrap(const parser::Unary *node) {
    auto *finally_block = this->builder_.CreateBasicBlock();

    this->builder_.SetupTryCatch(nullptr, finally_block);

    auto *ret = this->visit(node->value);

    this->builder_.AppendBasicBlock(finally_block);

    this->builder_.CreateUnaryOp(orbiter::OPCode::STRES, ret);

    this->builder_.CreateUnaryOp(orbiter::OPCode::TEND);

    return ret;
}

Instruction *IRBuilder::visitTryBlock(parser::TryBlock *node) {
    BasicBlock *catch_ctl = nullptr;
    BasicBlock *finally_block = nullptr;

    assert(!node->catches.empty() || node->finally != nullptr);

    if (!node->catches.empty())
        catch_ctl = this->builder_.CreateBasicBlock();

    finally_block = this->builder_.CreateBasicBlock();

    this->builder_.SetupTryCatch(catch_ctl, finally_block);

    JBlock ctx(&this->builder_, JBlockType::TCF);
    ctx.end = finally_block;

    this->sym_t_->EnterNestedScope(node->try_block->loc.start.offset);

    this->visit(node->try_block);

    this->sym_t_->LeaveNestedScope();

    // Skip the jump if there are no catch blocks,
    // since the finally block is right after the try block

    // Catch JMP Table
    if (!node->catches.empty()) {
        // Assume the last JMP instruction is generated by a return/break/continue statement
        if (!this->builder_.CheckIfLastInstructionIs(orbiter::OPCode::JMP))
            this->builder_.CreateJump(finally_block);

        this->builder_.AppendBasicBlock(catch_ctl);

        std::vector<BasicBlock *> catch_blocks;

        for (auto &entry: node->catches) {
            const auto *catch_block = (parser::CatchBlock *) entry.get();

            catch_blocks.push_back(this->builder_.CreateBasicBlock());

            // This catches everything because it has no type specified
            if (catch_block->catches.empty()) {
                this->builder_.CreateJump(catch_blocks.back());

                this->builder_.CreateAppendBasicBlock();
            }

            // These are compared and only catch errors of the correct type
            for (auto &type: catch_block->catches) {
                const auto errKey = this->visit(type.get());

                this->builder_.CreateJumpIfETypeMatch(errKey, catch_blocks.back());

                this->builder_.CreateAppendBasicBlock();
            }
        }

        this->builder_.CreateJump(finally_block);

        // Process catch body
        auto catch_index = 0;
        for (auto &entry: node->catches) {
            const auto *catch_block = (parser::CatchBlock *) entry.get();

            this->builder_.AppendBasicBlock(catch_blocks[catch_index++]);

            this->sym_t_->EnterNestedScope(catch_block->loc.start.offset);

            this->visit(catch_block->body);

            this->sym_t_->LeaveNestedScope();

            // Assume the last JMP instruction is generated by a return/break/continue statement
            if (!this->builder_.CheckIfLastInstructionIs(orbiter::OPCode::JMP))
                this->builder_.CreateJump(finally_block);
        }
    }

    this->builder_.AppendBasicBlock(finally_block);

    if (node->finally != nullptr) {
        const auto next_block = this->builder_.CreateBasicBlock();

        ctx.end = next_block;

        this->sym_t_->EnterNestedScope(node->finally->loc.start.offset);

        this->visit(node->finally);

        this->sym_t_->LeaveNestedScope();

        this->builder_.AppendBasicBlock(next_block);
    }

    this->builder_.CreateUnaryOp(orbiter::OPCode::TEND);

    return nullptr;
}

Instruction *IRBuilder::visitUnary(const parser::Unary *node) {
    Instruction *value = nullptr;

    // TODO: Implement Unary visitation

    if (node->node_type == parser::NodeType::UNARY) {
        value = this->visit(node->value);

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

    switch (node->node_type) {
        case parser::NodeType::NEW:
            return this->visitNew(node);
        case parser::NodeType::PANIC:
            value = this->visit(node->value);
            return this->builder_.CreateUnaryOp(orbiter::OPCode::PANIC, value);
        case parser::NodeType::TRAP:
            return this->visitTrap(node);
        case parser::NodeType::RETURN:
            return this->visitReturn(node);
        case parser::NodeType::NIL_SAFE:
            return this->visitNilSafety(node);
        case parser::NodeType::UPDATE:
            return this->visitUpdate(node);
        default:
            assert(false); // Never get here
    }

    // TODO: yield

    return nullptr;
}

Instruction *IRBuilder::visitUpdate(const parser::Unary *node) {
    const auto value = this->visit(node->value);

    const auto post = node->token_type == scanner::TokenType::PLUS_PLUS
                          ? this->builder_.CreateInc(value)
                          : this->builder_.CreateDec(value);

    if (node->value->node_type == parser::NodeType::IDENTIFIER) {
        this->StoreVariable(((parser::Identifier *) node->value)->symbol, post, false);

        return value;
    }

    if (node->value->node_type == parser::NodeType::SELECTOR) {
        const auto *sel_load = (LSObjectProp *) this->builder_.context->RFindFirstInstruction(orbiter::OPCode::LDOBJP);

        assert(sel_load != nullptr);

        this->builder_.StoreObjectProp(
            (Instruction *) sel_load->operands[0].value,
            post,
            sel_load->offset,
            sel_load->flags == orbiter::LoadObjectPropFlags::KEY);

        return value;
    }

    // INDEX

    assert(node->value->node_type == parser::NodeType::INDEX);

    const auto *last_load = (SubscrInstruction *) this->builder_.context->RFindFirstInstruction(orbiter::OPCode::LDIDX);

    assert(last_load != nullptr);

    this->builder_.CreateIndexStore(last_load, post);

    return value;
}

unsigned int IRBuilder::ProcessFunctionParams(const parser::Function *node, Instruction *&def_args,
                                              orbiter::LoadFuncFlags &f_flags) {
    def_args = nullptr;

    short def_params_count = 0;
    short remove_count = 0;

    for (auto &param: node->params) {
        if (param->node_type == parser::NodeType::PARAM)
            continue;

        if (param->node_type == parser::NodeType::NAMED_PARAM) {
            if (def_args == nullptr)
                def_args = this->builder_.CreateUnaryOp(orbiter::OPCode::NTUPLE, (U16) 0);

            const auto *named = (parser::Parameter *) param.get();

            const auto ld_const = this->builder_.LoadConstant((orbiter::datatype::OObject *) named->id);

            auto *value = named->value != nullptr ? this->visit(named->value) : this->builder_.LoadNilValue();

            this->builder_.CreateManip(orbiter::OPCode::ADDELEM, def_args, ld_const);
            this->builder_.CreateManip(orbiter::OPCode::ADDELEM, def_args, value);

            def_params_count += 1;
        }

        if (param->node_type == parser::NodeType::KW_PARAM) {
            f_flags |= orbiter::LoadFuncFlags::KW_PARAMS;

            remove_count += 1;
        }

        if (param->node_type == parser::NodeType::REST_PARAM) {
            f_flags |= orbiter::LoadFuncFlags::REST_PARAMS;

            remove_count += 1;
        }
    }

    if (def_args != nullptr) {
        ((UnaryImmInstr *) def_args)->imm = def_params_count * 2; // *2 because each param is composed by key/value

        f_flags |= orbiter::LoadFuncFlags::NPARAMS;
    }

    return (node->params.size() - def_params_count) - remove_count;
}

void IRBuilder::CaptureParametersIntoClosure(const parser::Function *node) {
    // Traverse the parameter list and, if a parameter is an UPVALUE, store it in the closure object.
    // This ensures the parameter is accessible in the closure's scope.

    for (const auto &cursor: node->params) {
        const auto *sym = this->sym_t_->Lookup(((parser::Parameter *) cursor.get())->id, cursor->loc.start.offset);

        assert(sym != nullptr);

        if (sym->type == SymbolType::PARAMETER && ENUMBITMASK_ISTRUE(sym->flags, SymbolFlags::UPVALUE)) {
            const auto value = this->LoadParameter(sym);

            this->StoreVariable(sym, value, false);
        }
    }
}

void IRBuilder::BuildMethodEmptyBody(const parser::Function *node) {
    using namespace orbiter::datatype;

    const auto msg = ORStringFormat(this->isolate_,
                                    NotImplementedError::Details[NotImplementedError::Reason::METHOD],
                                    node->name->buffer);

    auto *keyInstr = this->builder_.LoadAtomConstant(NotImplementedError::Details[NotImplementedError::Reason::ID]);
    auto *msgInstr = this->builder_.LoadConstant((OObject *) msg.get());
    auto *details = this->builder_.LoadNilValue();

    auto *error = this->builder_.CreateError(keyInstr, msgInstr, details);

    this->builder_.CreateUnaryOp(orbiter::OPCode::PANIC, error);
}

void IRBuilder::PutSyncExit(const JBlock *block) {
    const auto *cursor = this->builder_.context->j_chain;

    while (cursor != block) {
        if (cursor->type == JBlockType::SYNC)
            this->builder_.CreateUnaryOp(orbiter::OPCode::SYNC_EXIT, cursor->value);

        cursor = cursor->prev;
    }
}

void IRBuilder::VisitForInLoop(const parser::Loop *node) {
    const JBlock jb(&this->builder_, JBlockType::FOR_IN, nullptr);

    assert(false); // TODO: IMPL THIS!

    this->sym_t_->EnterNestedScope(node->loc.start.offset);

    this->sym_t_->LeaveNestedScope();
}

IRCHandle IRBuilder::Generate(const parser::ASTHandle<parser::Module *> &module) noexcept {
    assert(this->isolate_ == module->isolate); // Security check.

    try {
        // Set a symbol table
        this->sym_t_ = module->sym_t;

        // Create first context
        this->builder_.IRContextNew(IRContextType::MODULE, this->sym_t_->scope->GetLocalVariableCount());

        auto *context = this->builder_.context;

        for (auto &statement: module->statements)
            this->visit(statement.get());

        assert(this->builder_.context == context);

        // Insert return value
        if (!this->builder_.CheckIfLastInstructionIs(orbiter::OPCode::RET))
            this->builder_.CreateReturn(0);

        // This call ensures any checks performed by LeaveContext are honored
        this->builder_.LeaveContext();

        this->builder_.context = nullptr;

        return IRCHandle(context);
    } catch (const SymbolTableException &) {
        assert(false);
    } catch (...) {
        assert(false);
    }

    return {};
}
