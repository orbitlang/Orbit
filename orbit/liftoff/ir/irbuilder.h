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

        class CTContext *ct_active_ = nullptr;

        orbiter::Isolate *isolate_ = nullptr;

        SymbolTable *sym_t_ = nullptr;

        OptimizationLevel level_;

        bool is_module_;

        friend class ASTVisitor;

        friend class CTContext;

        Instruction *BinaryOP(const parser::Binary *binary);

        Instruction *CreateCall(const parser::Call *node, Instruction *f_src);

        Instruction *CreateJumpForElvisOrNil(const parser::Binary *binary, orbiter::OPCode opcode);

        Instruction *LoadVariable(const Symbol *symbol);

        Instruction *LoadParameter(const Symbol *symbol);

        Instruction *LoadSelfParam(MSize offset);

        Instruction *StoreVariable(const Symbol *symbol, Instruction *value, bool decl);

        Instruction *visitASTNode(parser::ASTNode *node);

        Instruction *visitAssignment(parser::Assignment *node);

        Instruction *visitBinary(parser::Binary *node);

        Instruction *visitBlock(const parser::Block *node);

        Instruction *visitBranch(const parser::Branch *node);

        Instruction *visitCall(parser::Call *node);

        Instruction *visitCatchBlock(parser::CatchBlock *node);

        Instruction *visitConstruct(const parser::Construct *node);

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

        Instruction *visitNew(const parser::Unary *node);

        Instruction *visitParameter(parser::Parameter *node);

        Instruction *visitSelector(parser::Selector *node);

        Instruction *visitSubscript(parser::Subscript *node);

        Instruction *visitSwitchCase(parser::SwitchCase *node);

        Instruction *visitSwitchBlock(parser::SwitchBlock *node);

        Instruction *visitTryBlock(parser::TryBlock *node);

        Instruction *visitUnary(const parser::Unary *node);

        unsigned int ProcessFunctionParams(const parser::Function *node, Instruction *&def_args,
                                           orbiter::LoadFuncFlags &f_flags);

        void CaptureParametersIntoClosure(const parser::Function *node);

        void VisitForInLoop(const parser::Loop *node);

        void PutSyncExit(const JBlock *block);

    public:
        explicit IRBuilder(orbiter::Isolate *isolate,
                           const OptimizationLevel level,
                           const bool is_module) noexcept: builder_(isolate),
                                                           isolate_(isolate),
                                                           level_(level),
                                                           is_module_(is_module) {
        }

        [[nodiscard]] IRCHandle Generate(const parser::ASTHandle<parser::Module *> &module) noexcept;
    };

    class CTContext final {
        IRBuilder *builder = nullptr;

        CTContext *prev = nullptr;

        Instruction *tp_ptr = nullptr;

        std::vector<parser::Assignment *> properties;

        friend class IRBuilder;

    public:
        explicit CTContext(IRBuilder *builder, Instruction *tp_ptr) : tp_ptr(tp_ptr) {
            this->builder = builder;

            this->prev = builder->ct_active_;

            builder->ct_active_ = this;
        }

        ~CTContext() {
            this->builder->ct_active_ = this->prev;
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_IRBUILDER_H_
