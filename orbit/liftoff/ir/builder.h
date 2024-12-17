// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_BUILDER_H_
#define ORBIT_LIFTOFF_IR_BUILDER_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/liftoff/ir/ircontext.h>
#include <orbit/liftoff/ir/value.h>

namespace liftoff::ir {
    class Builder {
        orbiter::IsolateAllocator allocator_;

        orbiter::Isolate *isolate_;

        /**
         * @brief Adds an instruction to the current basic block.
         *
         * @param instruction The instruction to add.
         * @return Pointer to the basic block.
         */
        BasicBlock *AddInstruction(Instruction *instruction) {
            auto *bb = this->context->current_;
            if (bb == nullptr)
                bb = this->CreateAppendBasicBlock();

            bb->AddInstruction(instruction);

            return bb;
        }

        Instruction *LoadImmConstant(orbiter::LoadConstantMode mode);

        /**
         * @brief Loads or stores using the specified opcode and offset.
         *
         * @param opcode The opcode for the load/store operation.
         * @param offset The offset for the load/store operation.
         * @return Pointer to the created instruction.
         */
        Instruction *LoadStoreOffset(orbiter::OPCode opcode, I16 offset);

        template<typename T, typename... Args>
        std::enable_if_t<std::is_base_of_v<Object, T>, T *> CreateObject(Args... args) {
            auto *obj = this->allocator_.alloc<T>(sizeof(T));
            if (obj == nullptr)
                throw std::bad_alloc();

            new(obj)T(args...);

            this->AddToObjsList(obj);

            return obj;
        }

        /**
         * @brief Adds an object to the object list.
         *
         * @param obj The object to add.
         */
        void AddToObjsList(Object *obj) const noexcept;

        /**
         * @brief Removes an object from the object list.
         *
         * @param obj The object to remove.
         */
        void RemoveFromObjsList(Object *obj) const noexcept;

    public:
        IRContext *context = nullptr;

        /**
         * @brief Constructs a new Builder object.
         *
         * @param isolate Pointer to isolate.
         */
        explicit Builder(orbiter::Isolate *isolate) noexcept: allocator_(isolate), isolate_(isolate) {
        }

        [[nodiscard]] BasicBlock *GetCurrentBasicBlock() const noexcept {
            return this->context->current_;
        }

        Instruction *AllocStackSlots(U16 slots, orbiter::AllocaFlags flags);

        Instruction *CreateBinaryOp(orbiter::OPCode opcode, Object *left, Object *right);

        Instruction *CreateBinaryOpFlags(orbiter::OPCode opcode, U8 flags, Object *left, Object *right);

        Instruction *CreateBranch(orbiter::OPCode opcode, Object *value, BasicBlock *continuation,
                                  BasicBlock *destination);

        Instruction *CreateCall(Object *src, U16 arguments);

        Instruction *CreateJump(BasicBlock *destination);

        Instruction *CreateUnaryOp(orbiter::OPCode opcode, Object *s_reg);

        Instruction *CreateUnaryOp(orbiter::OPCode opcode, U16 imm, U8 flags);

        Instruction *CreateUnaryOp(orbiter::OPCode opcode, U16 imm) {
            return this->CreateUnaryOp(opcode, imm, 0);
        }

        Instruction *LoadConstant(U16 offset) {
            return this->LoadStoreOffset(orbiter::OPCode::LDCST, offset);
        }

        Instruction *LoadFromStackOffset(I16 offset) {
            return this->LoadStoreOffset(orbiter::OPCode::SKLDR, offset);
        }

        Instruction *LoadNilValue() {
            return this->LoadImmConstant(orbiter::LoadConstantMode::NIL);
        }

        Instruction *LoadTrueValue() {
            return this->LoadImmConstant(orbiter::LoadConstantMode::TRUE);
        }

        Instruction *LoadFalseValue() {
            return this->LoadImmConstant(orbiter::LoadConstantMode::FALSE);
        }

        Instruction *LoadCodeObject(U16 offset) {
            auto *instr = this->CreateObject<LoadCodeInstr>(offset);

            this->AddInstruction(instr);

            instr->dest.virtID = this->context->GetIncRVirtCounter();

            return instr;
        }

        Instruction *LoadFunction(Object *src, orbiter::LoadFuncFlags flags);

        Instruction *LoadImmediate(MachineSize value);

        Instruction *LoadFromClosureAtOffset(I16 offset, orbiter::ClosureLSMode mode) {
            auto *instr = this->CreateObject<LoadStoreClosureWithOffsetInstr>(
                    orbiter::OPCode::CLOLDR,
                    offset,
                    mode,
                    nullptr);

            this->AddInstruction(instr);

            return instr;
        }

        Instruction *StoreToClosureAtOffset(Object *src, I16 offset, orbiter::ClosureLSMode mode) {
            auto *instr = this->CreateObject<LoadStoreClosureWithOffsetInstr>(orbiter::OPCode::CLOSTR,
                                                                              offset,
                                                                              mode,
                                                                              src);

            this->AddInstruction(instr);

            return instr;
        }

        Instruction *StoreToStackOffset(Object *src, I16 offset) {
            auto *instr = (LoadStoreWithOffsetInstr *) this->LoadStoreOffset(orbiter::OPCode::SKSTR, offset);

            instr->src = src;

            return instr;
        }

        Instruction *CreateReturn(Object *s_reg, bool yield);

        Instruction *StackPop();

        Instruction *StackPush(Object *s_reg);

        U16 IRContextNew(IRContextType type);

        PhiInstr *CreatePhi();

        Value *CreateImmediateValue(const U16 value) {
            return this->CreateObject<Value>(value);
        }

        BasicBlock *CreateBasicBlock() {
            return this->CreateObject<BasicBlock>();
        }

        BasicBlock *CreateAppendBasicBlock() {
            auto *bb = this->CreateBasicBlock();

            this->AppendBasicBlock(bb);

            return bb;
        }

        /**
         * @brief Appends a basic block to the current context.
         *
         * @param bb The basic block to append.
         */
        void AppendBasicBlock(BasicBlock *bb) const noexcept;

        /**
         * @brief Deletes a basic block from the current context..
         *
         * @param bb The basic block to delete.
         */
        void DeleteBasicBlock(BasicBlock *bb) const noexcept;

        void LeaveContext() noexcept {
            this->context = this->context->back;
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_BUILDER_H_
