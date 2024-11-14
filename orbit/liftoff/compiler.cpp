// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/compiler.h>

#include "parser/parser.h"

using namespace liftoff;
using namespace liftoff::parser;

ASTNode *Compiler::visitASTNode(ASTNode *node) {
    // TODO: Implement ASTNode visitation
    return node;
}

ASTNode *Compiler::visitAssignment(Assignment *node) {
    // TODO: Implement Assignment visitation
    return node;
}

ASTNode *Compiler::visitBinary(Binary *node) {
    switch (node->node_type) {
        case NodeType::BINARY:
            this->visit(node->left);
            // TODO: Make OP
            this->visit(node->right);
            break;
        default:
            assert(false);
    }
    return node;
}

ASTNode *Compiler::visitBlock(Block *node) {
    // TODO: Implement Block visitation
    return node;
}

ASTNode *Compiler::visitBranch(Branch *node) {
    // TODO: Implement Branch visitation
    return node;
}

ASTNode *Compiler::visitCall(Call *node) {
    // TODO: Implement Call visitation
    return node;
}

ASTNode *Compiler::visitCatchBlock(CatchBlock *node) {
    // TODO: Implement CatchBlock visitation
    return node;
}

ASTNode *Compiler::visitConstruct(Construct *node) {
    // TODO: Implement Construct visitation
    return node;
}

ASTNode *Compiler::visitDecorator(Decorator *node) {
    // TODO: Implement Decorator visitation
    return node;
}

ASTNode *Compiler::visitFunction(Function *node) {
    // TODO: Implement Function visitation
    return node;
}

ASTNode *Compiler::visitIdentifier(Identifier *node) {
    // TODO: Implement Identifier visitation
    return node;
}

ASTNode *Compiler::visitImport(Import *node) {
    // TODO: Implement Import visitation
    return node;
}

ASTNode *Compiler::visitImportName(ImportName *node) {
    // TODO: Implement ImportName visitation
    return node;
}

ASTNode *Compiler::visitJump(Jump *node) {
    // TODO: Implement Jump visitation
    return node;
}

ASTNode *Compiler::visitLabel(Label *node) {
    // TODO: Implement Label visitation
    return node;
}

ASTNode *Compiler::visitListExpression(ListExpression *node) {
    // TODO: Implement ListExpression visitation
    return node;
}

ASTNode *Compiler::visitLiteral(Literal *node) {
    // Il numero lo carico direttamente
    // Anche nil e i bool
    // Gli oggetti li scrivo nella memoria statica
    return node;
}

ASTNode *Compiler::visitLoop(Loop *node) {
    // TODO: Implement Loop visitation
    return node;
}

ASTNode *Compiler::visitModule(Module *node) {
    // TODO: Implement Module visitation
    return node;
}

ASTNode *Compiler::visitNativeFunc(NativeFunc *node) {
    // TODO: Implement NativeFunc visitation
    return node;
}

ASTNode *Compiler::visitNativeParameter(NativeParameter *node) {
    // TODO: Implement NativeParameter visitation
    return node;
}

ASTNode *Compiler::visitNativeVariable(NativeVariable *node) {
    // TODO: Implement NativeVariable visitation
    return node;
}

ASTNode *Compiler::visitParameter(Parameter *node) {
    // TODO: Implement Parameter visitation
    return node;
}

ASTNode *Compiler::visitSubscript(Subscript *node) {
    // TODO: Implement Subscript visitation
    return node;
}

ASTNode *Compiler::visitSwitchCase(SwitchCase *node) {
    // TODO: Implement SwitchCase visitation
    return node;
}

ASTNode *Compiler::visitSwitchBlock(SwitchBlock *node) {
    // TODO: Implement SwitchBlock visitation
    return node;
}

ASTNode *Compiler::visitTryBlock(TryBlock *node) {
    // TODO: Implement TryBlock visitation
    return node;
}

ASTNode *Compiler::visitUnary(Unary *node) {
    // TODO: Implement Unary visitation
    return node;
}

orbiter::datatype::OObject *Compiler::compile(ASTHandle<Module *> &module) {
    assert(this->isolate_ == module->isolate); // Security check.

    for (auto &statement: module->statements) {
        this->visit(statement.get());
    }

    return {};
}
