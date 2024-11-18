// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/irbuilder.h>

using namespace liftoff::ir;

Object *IRBuilder::BinaryOP(parser::Binary *binary) {
    const auto *nr = (parser::Binary *) binary->right;

    Object *left;
    Object *right;

    bool left2right = true;

    if (nr->node_type == parser::NodeType::BINARY
        && (nr->token_type == scanner::TokenType::ASTERISK
            || nr->token_type == scanner::TokenType::SLASH
            || nr->token_type == scanner::TokenType::SLASH_SLASH)) {
        left2right = false;
    }

    if (left2right) {
        left = this->visit(binary->left);
        right = this->visit(binary->right);
    } else {
        right = this->visit(binary->right);
        left = this->visit(binary->left);
    }

    orbiter::OPCode op_code{};

    switch (binary->token_type) {
        case scanner::TokenType::PLUS:
            op_code = orbiter::OPCode::ADD;
            break;
        case scanner::TokenType::MINUS:
            // TODO
            break;
        case scanner::TokenType::ASTERISK:
            op_code = orbiter::OPCode::MUL;
            break;
        case scanner::TokenType::SLASH:
            op_code = orbiter::OPCode::DIV;
            break;
        case scanner::TokenType::SLASH_SLASH:
            // TODO
            break;
        default:
            assert(false); // Never get here
    }

    return this->builder_.CreateBinaryOp(op_code, left, right);
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
