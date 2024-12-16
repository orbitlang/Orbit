// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_IRBUILDER_H_
#define ORBIT_LIFTOFF_IR_IRBUILDER_H_

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/liftoff/ir/builder.h>

#include <orbit/liftoff/parser/ast.h>

namespace liftoff::ir {
    class IRBuilder : parser::ASTVisitor<IRBuilder, Object *> {
        Builder builder_;

        orbiter::Isolate *isolate_ = nullptr;

        SymbolTable *sym_t_;

        friend class ASTVisitor;

        Object *BinaryOP(const parser::Binary *binary);

        Object *CreateJumpForElvisOrNil(const parser::Binary *binary, orbiter::OPCode opcode);

        Object *LoadVariable(const Symbol *symbol);

        Object *StoreVariable(const Symbol *symbol, Object *value);

        Object *visitASTNode(parser::ASTNode *node);

        Object *visitAssignment(parser::Assignment *node);

        Object *visitBinary(parser::Binary *node);

        Object *visitBlock(const parser::Block *node);

        Object *visitBranch(const parser::Branch *node);

        Object *visitCall(parser::Call *node);

        Object *visitCatchBlock(parser::CatchBlock *node);

        Object *visitConstruct(parser::Construct *node);

        Object *visitDecorator(parser::Decorator *node);

        Object *visitFunction(const parser::Function *node);

        Object *visitIdentifier(parser::Identifier *node);

        Object *visitImport(parser::Import *node);

        Object *visitImportName(parser::ImportName *node);

        Object *visitJump(const parser::Jump *node);

        Object *visitLabel(const parser::Label *node);

        Object *visitListExpression(parser::ListExpression *node);

        Object *visitLiteral(parser::Literal *node);

        Object *visitLoop(const parser::Loop *node);

        Object *visitModule(parser::Module *node);

        Object *visitNativeFunc(parser::NativeFunc *node);

        Object *visitNativeParameter(parser::NativeParameter *node);

        Object *visitNativeVariable(parser::NativeVariable *node);

        Object *visitParameter(parser::Parameter *node);

        Object *visitSubscript(parser::Subscript *node);

        Object *visitSwitchCase(parser::SwitchCase *node);

        Object *visitSwitchBlock(parser::SwitchBlock *node);

        Object *visitTryBlock(parser::TryBlock *node);

        Object *visitUnary(const parser::Unary *node);

        void CaptureParametersIntoClosure(const parser::Function *node);

        void visitForInLoop(const parser::Loop *node);

        void PutSyncExit(const JBlock *block);
    public:
        explicit IRBuilder(orbiter::Isolate *isolate) : builder_(isolate), isolate_(isolate) {
        }

        [[nodiscard]] IRContext *Generate(parser::ASTHandle<parser::Module *> &module);
    };
}

#endif // !ORBIT_LIFTOFF_IR_IRBUILDER_H_
