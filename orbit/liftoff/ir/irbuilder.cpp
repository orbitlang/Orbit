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

Object *IRBuilder::BinaryOP(const parser::Binary *binary) {
    const auto *nr = (parser::Binary *) binary->right;

    Object *left;
    Object *right = nullptr;

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
            auto number = (PtrSize) literal->literal;

            number >>= 1;

            // Max immediate size: <= 0xFFFF
            if (number <= 0xFFFF) {
                right = this->builder_.CreateImmediateValue((U16) number);
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

    auto op_code = InfixOp2OpCode(binary->token_type, right->type() == ObjectType::VALUE, op_flags);

    return this->builder_.CreateBinaryOpFlags(op_code, (U8) op_flags, left, right);
}


Object *IRBuilder::visitASTNode(parser::ASTNode *node) {
    // TODO: Implement ASTNode visitation
    return nullptr;
}

Object *IRBuilder::visitAssignment(parser::Assignment *node) {
    // TODO: Implement Assignment visitation

    return nullptr;
}

Object *IRBuilder::visitBinary(parser::Binary *node) {
    switch (node->node_type) {
        case parser::NodeType::BINARY:
            return this->BinaryOP(node);
        default:
            assert(false);
    }
    return nullptr;
}

Object *IRBuilder::visitBlock(parser::Block *node) {
    // TODO: Implement Block visitation
    return nullptr;
}

Object *IRBuilder::visitBranch(parser::Branch *node) {
    // TODO: Implement Branch visitation
    return nullptr;
}

Object *IRBuilder::visitCall(parser::Call *node) {
    // TODO: Implement Call visitation
    return nullptr;
}

Object *IRBuilder::visitCatchBlock(parser::CatchBlock *node) {
    // TODO: Implement CatchBlock visitation
    return nullptr;
}

Object *IRBuilder::visitConstruct(parser::Construct *node) {
    // TODO: Implement Construct visitation
    return nullptr;
}

Object *IRBuilder::visitDecorator(parser::Decorator *node) {
    // TODO: Implement Decorator visitation
    return nullptr;
}

Object *IRBuilder::visitFunction(parser::Function *node) {
    // TODO: Implement Function visitation
    return nullptr;
}

Object *IRBuilder::visitIdentifier(parser::Identifier *node) {
    const auto *sym = this->sym_t_->Lookup(node->value, node->loc.start.offset);

    assert(sym != nullptr);

    if (sym->type == SymbolType::VARIABLE) {
        return this->builder_.LoadFromStackOffset(sym->offset);
    }

    assert(false);
    return nullptr;
}

Object *IRBuilder::visitImport(parser::Import *node) {
    // TODO: Implement Import visitation
    return nullptr;
}

Object *IRBuilder::visitImportName(parser::ImportName *node) {
    // TODO: Implement ImportName visitation
    return nullptr;
}

Object *IRBuilder::visitJump(parser::Jump *node) {
    // TODO: Implement Jump visitation
    return nullptr;
}

Object *IRBuilder::visitLabel(parser::Label *node) {
    // TODO: Implement Label visitation
    return nullptr;
}

Object *IRBuilder::visitListExpression(parser::ListExpression *node) {
    // TODO: Implement ListExpression visitation
    return nullptr;
}

Object *IRBuilder::visitLiteral(parser::Literal *node) {
    // TODO: Implement Literal visitation
    if (!O_IS_OBJECT(node->literal))
        return this->builder_.LoadImmediate((MachineSize) node->literal);

    return nullptr;
}

Object *IRBuilder::visitLoop(parser::Loop *node) {
    // TODO: Implement Loop visitation
    return nullptr;
}

Object *IRBuilder::visitModule(parser::Module *node) {
    // TODO: Implement Module visitation
    return nullptr;
}

Object *IRBuilder::visitNativeFunc(parser::NativeFunc *node) {
    // TODO: Implement NativeFunc visitation
    return nullptr;
}

Object *IRBuilder::visitNativeParameter(parser::NativeParameter *node) {
    // TODO: Implement NativeParameter visitation
    return nullptr;
}

Object *IRBuilder::visitNativeVariable(parser::NativeVariable *node) {
    // TODO: Implement NativeVariable visitation
    return nullptr;
}

Object *IRBuilder::visitParameter(parser::Parameter *node) {
    // TODO: Implement Parameter visitation
    return nullptr;
}

Object *IRBuilder::visitSubscript(parser::Subscript *node) {
    // TODO: Implement Subscript visitation
    return nullptr;
}

Object *IRBuilder::visitSwitchCase(parser::SwitchCase *node) {
    // TODO: Implement SwitchCase visitation
    return nullptr;
}

Object *IRBuilder::visitSwitchBlock(parser::SwitchBlock *node) {
    // TODO: Implement SwitchBlock visitation
    return nullptr;
}

Object *IRBuilder::visitTryBlock(parser::TryBlock *node) {
    // TODO: Implement TryBlock visitation
    return nullptr;
}

Object *IRBuilder::visitUnary(parser::Unary *node) {
    // TODO: Implement Unary visitation
    return nullptr;
}

void *IRBuilder::Generate(parser::ASTHandle<parser::Module *> &module) {
    assert(this->isolate_ == module->isolate); // Security check.

    // Set symbol table
    this->sym_t_ = module->sym_t;

    // Create first context
    auto ir_module = this->builder_.CreateModule();

    for (auto &statement: module->statements) {
        this->visit(statement.get());
    }

    return nullptr;
}
