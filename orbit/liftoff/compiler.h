// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_COMPILER_H_
#define ORBIT_LIFTOFF_COMPILER_H_

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/liftoff/parser/ast.h>

namespace liftoff {
    class Compiler : parser::ASTVisitor<Compiler> {
        orbiter::Isolate *isolate_ = nullptr;

        friend class ASTVisitor;

        parser::ASTNode *visitASTNode(parser::ASTNode *node);

        parser::ASTNode *visitAssignment(parser::Assignment *node);

        parser::ASTNode *visitBinary(parser::Binary *node);

        parser::ASTNode *visitBlock(parser::Block *node);

        parser::ASTNode *visitBranch(parser::Branch *node);

        parser::ASTNode *visitCall(parser::Call *node);

        parser::ASTNode *visitCatchBlock(parser::CatchBlock *node);

        parser::ASTNode *visitConstruct(parser::Construct *node);

        parser::ASTNode *visitDecorator(parser::Decorator *node);

        parser::ASTNode *visitFunction(parser::Function *node);

        parser::ASTNode *visitIdentifier(parser::Identifier *node);

        parser::ASTNode *visitImport(parser::Import *node);

        parser::ASTNode *visitImportName(parser::ImportName *node);

        parser::ASTNode *visitJump(parser::Jump *node);

        parser::ASTNode *visitLabel(parser::Label *node);

        parser::ASTNode *visitListExpression(parser::ListExpression *node);

        parser::ASTNode *visitLiteral(parser::Literal *node);

        parser::ASTNode *visitLoop(parser::Loop *node);

        parser::ASTNode *visitModule(parser::Module *node);

        parser::ASTNode *visitNativeFunc(parser::NativeFunc *node);

        parser::ASTNode *visitNativeParameter(parser::NativeParameter *node);

        parser::ASTNode *visitNativeVariable(parser::NativeVariable *node);

        parser::ASTNode *visitParameter(parser::Parameter *node);

        parser::ASTNode *visitSubscript(parser::Subscript *node);

        parser::ASTNode *visitSwitchCase(parser::SwitchCase *node);

        parser::ASTNode *visitSwitchBlock(parser::SwitchBlock *node);

        parser::ASTNode *visitTryBlock(parser::TryBlock *node);

        parser::ASTNode *visitUnary(parser::Unary *node);

    public:
        explicit Compiler(orbiter::Isolate *isolate) : isolate_(isolate) {
        }

        orbiter::datatype::OObject *compile(parser::ASTHandle<parser::Module *> &module);
    };
}

#endif // |ORBIT_LIFTOFF_COMPILER_H_
