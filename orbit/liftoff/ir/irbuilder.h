// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_IRBUILDER_H_
#define ORBIT_LIFTOFF_IR_IRBUILDER_H_

#include <orbit/liftoff/ir/builder.h>

#include <orbit/liftoff/parser/ast.h>

#include <orbit/liftoff/olevel.h>

namespace liftoff::ir {
    class IRBuilder : parser::ASTVisitor<IRBuilder, Instruction *> {
        Builder builder_;

        orbiter::Isolate *isolate_ = nullptr;

        SymbolTable *sym_t_ = nullptr;

        OptimizationLevel level_;

        friend class ASTVisitor;

        Instruction *BinaryOP(const parser::Binary *binary);

        Instruction *CreateJumpForElvisOrNil(const parser::Binary *binary, orbiter::OPCode opcode);

        Instruction *LoadVariable(const Symbol *symbol);

        Instruction *StoreVariable(const Symbol *symbol, Instruction *value, bool decl);

        Instruction *visitASTNode(parser::ASTNode *node);

        Instruction *visitAssignment(parser::Assignment *node);

        Instruction *visitBinary(parser::Binary *node);

        Instruction *visitBlock(const parser::Block *node);

        Instruction *visitBranch(const parser::Branch *node);

        Instruction *visitCall(parser::Call *node);

        Instruction *visitCatchBlock(parser::CatchBlock *node);

        Instruction *visitConstruct(parser::Construct *node);

        Instruction *visitDecorator(parser::Decorator *node);

        Instruction *visitFunction(const parser::Function *node);

        Instruction *visitIdentifier(parser::Identifier *node);

        Instruction *visitImport(parser::Import *node);

        Instruction *visitImportName(parser::ImportName *node);

        Instruction *visitJump(const parser::Jump *node);

        Instruction *visitLabel(const parser::Label *node);

        Instruction *visitListExpression(parser::ListExpression *node);

        Instruction *visitLiteral(parser::Literal *node);

        Instruction *visitLoop(const parser::Loop *node);

        Instruction *visitModule(parser::Module *node);

        Instruction *visitNativeFunc(parser::NativeFunc *node);

        Instruction *visitNativeParameter(parser::NativeParameter *node);

        Instruction *visitNativeVariable(parser::NativeVariable *node);

        Instruction *visitParameter(parser::Parameter *node);

        Instruction *visitSubscript(parser::Subscript *node);

        Instruction *visitSwitchCase(parser::SwitchCase *node);

        Instruction *visitSwitchBlock(parser::SwitchBlock *node);

        Instruction *visitTryBlock(parser::TryBlock *node);

        Instruction *visitUnary(const parser::Unary *node);

        void CaptureParametersIntoClosure(const parser::Function *node);

        void visitForInLoop(const parser::Loop *node);

        void PutSyncExit(const JBlock *block);

    public:
        explicit IRBuilder(orbiter::Isolate *isolate, OptimizationLevel level) : builder_(isolate),
            isolate_(isolate),
            level_(level) {
        }

        [[nodiscard]] IRContext *Generate(parser::ASTHandle<parser::Module *> &module) noexcept;
    };
}

#endif // !ORBIT_LIFTOFF_IR_IRBUILDER_H_
