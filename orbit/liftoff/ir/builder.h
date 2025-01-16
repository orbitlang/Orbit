// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_BUILDER_H_
#define ORBIT_LIFTOFF_IR_BUILDER_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/liftoff/ir/ircontext.h>
#include <orbit/liftoff/ir/value.h>

namespace liftoff::ir {
    /**
     * @brief Builder class for constructing Intermediate Representation (IR) instructions and basic blocks.
     *
     * The Builder class provides methods to create, append, and manipulate instructions, objects,
     * and basic blocks within an intermediate representation context. It facilitates the construction
     * of an IR for code generation purposes and supports functionalities such as handling stack
     * operations, constants, and various opcodes.
     */
    class Builder {
        orbiter::IsolateAllocator allocator_;

        orbiter::Isolate *isolate_;

        template<typename T, typename... Args>
        /** @brief Creates an object of a specified type and adds it to the object list.
         *
         * @tparam T Type of the object to create. Must be derived from Object.
         * @tparam Args Variadic template arguments for the object's constructor.
         * @param args Constructor arguments to initialize the object.
         *
         * @return Pointer to the created object.
         * @throws std::bad_alloc If memory allocation fails.
         */
        std::enable_if_t<std::is_base_of_v<Object, T>, T *> CreateObject(Args... args) {
            auto *obj = this->allocator_.alloc<T>(sizeof(T));
            if (obj == nullptr)
                throw std::bad_alloc();

            new(obj)T(args...);

            this->AddToObjsList(obj);

            return obj;
        }

        template<typename T, typename... Args>
        /**
         * @brief Creates an instruction of a specified type and adds it to the current basic block.
         *
         * This method first constructs an instruction object of type T using the provided arguments and then adds the created instruction to the current basic block.
         *
         * @tparam T The type of the instruction to create. Must be derived from the Instruction class.
         * @tparam Args Variadic template types representing the arguments for the constructor of T.
         * @param args Arguments used to construct the instruction.
         *
         * @return A pointer to the created instruction of type T.
         */
        std::enable_if_t<std::is_base_of_v<Instruction, T>, T *> CreateInstruction(Args... args) {
            auto *instr = this->CreateObject<T>(args...);

            this->AddInstruction(instr);

            return instr;
        }

        /**
         * @brief Adds an instruction to the current basic block.
         *
         * @param instruction The instruction to add.
         *
         * @return Pointer to the basic block.
         */
        BasicBlock *AddInstruction(Instruction *instruction);

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

        /**
         * @brief Creates and appends a new basic block to the builder's context.
         *
         * This method constructs a new `BasicBlock` instance, appends it to the
         * current list of basic blocks, and returns the created `BasicBlock`.
         *
         * @return A pointer to the newly created and appended `BasicBlock`.
         */
        BasicBlock *CreateAppendBasicBlock();

        /**
         * @brief Creates a new basic block and adds it to the object list.
         *
         * This method creates a new instance of the BasicBlock class and
         * registers it within the internal object management system.
         *
         * @return Pointer to the created BasicBlock object.
         */
        [[nodiscard]] BasicBlock *CreateBasicBlock() {
            return this->CreateObject<BasicBlock>();
        }

        Instruction *AllocStackSlots(U16 slots, orbiter::AllocaFlags flags);

        Instruction *CreateBinaryOp(orbiter::OPCode opcode, Instruction *left, Instruction *right);

        Instruction *CreateBinaryOpFlags(orbiter::OPCode opcode, U8 flags, Instruction *left, Instruction *right);

        Instruction *CreateBinaryOpFlags(orbiter::OPCode opcode, U8 flags, Instruction *left, U16 right);

        Instruction *CreateBranch(orbiter::OPCode opcode, Instruction *value, BasicBlock *continuation,
                                  BasicBlock *destination);

        Instruction *CreateCall(Instruction *src, U16 arguments);

        Instruction *CreateJump(BasicBlock *destination);

        Instruction *CreateStoreVariable(orbiter::OPCode opcode, I16 offset, U8 flags, Instruction *value);

        Instruction *CreateReturn(Instruction *s_reg, bool yield);

        Instruction *CreateUnaryOp(orbiter::OPCode opcode, Instruction *s_reg);

        Instruction *CreateUnaryOp(orbiter::OPCode opcode, U16 imm, U8 flags);

        Instruction *CreateUnaryOp(orbiter::OPCode opcode, U16 imm) {
            return this->CreateUnaryOp(opcode, imm, 0);
        }

        Instruction *LoadCodeObject(U16 offset);

        Instruction *LoadConstant(U16 offset);

        Instruction *LoadFalseValue() {
            return this->CreateInstruction<
                UnaryImmInstr>(orbiter::OPCode::LDCST, (U8) orbiter::LoadConstantMode::FALSE);
        }

        Instruction *LoadFromClosureAtOffset(I16 offset, orbiter::ClosureLSMode mode);

        Instruction *LoadFromStackOffset(I16 offset);

        Instruction *LoadFunction(Instruction *src, orbiter::LoadFuncFlags flags);

        Instruction *LoadImmediate(MachineSize value);

        Instruction *LoadNilValue() {
            return this->CreateInstruction<UnaryImmInstr>(orbiter::OPCode::LDCST, (U8) orbiter::LoadConstantMode::NIL);
        }

        /**
         * @brief Loads or stores using the specified opcode and offset.
         *
         * @param opcode The opcode for the load/store operation.
         * @param offset The offset for the load/store operation.
         * @param flags flags
         *
         * @return Pointer to the created instruction.
         */
        Instruction *LoadFromOffset(orbiter::OPCode opcode, I16 offset, U8 flags);

        Instruction *LoadTrueValue() {
            return this->CreateInstruction<UnaryImmInstr>(orbiter::OPCode::LDCST, (U8) orbiter::LoadConstantMode::TRUE);
        }

        Instruction *GetLoadFromStackOffset(I16 offset) {
            return this->CreateObject<OffsetInstruction>(orbiter::OPCode::SKLDR, offset);
        }

        Instruction *GetStoreToStackOffset(Instruction *src, I16 offset) {
            return this->CreateObject<OffsetInstruction>(orbiter::OPCode::SKSTR, offset, src);
        }

        Instruction *StackPop();

        Instruction *StackPush(Instruction *s_reg);

        Instruction *StoreToClosureAtOffset(Instruction *src, I16 offset, orbiter::ClosureLSMode mode);

        Instruction *StoreToStackOffset(Instruction *src, I16 offset);

        PhiInstr *CreatePhi();

        /**
         * @brief Creates a new intermediate representation (IR) context of the specified type.
         *
         * The method allocates memory for a new IR context, initializes it with the provided type,
         * handles it within the current context stack, and invalidates active variables
         * before associating the new context as the current context.
         *
         * @param type The type of the IR context to be created.
         *
         * @return The identifier of the new IR context as a U16 type.
         *
         * @throws std::bad_alloc if memory allocation for the new IR context fails.
         * @throws Any exception propagated from context management operations.
         */
        U16 IRContextNew(IRContextType type);

        /*
        Value *CreateImmediateValue(const U16 value) {
            return this->CreateObject<Value>(value);
        }
        */

        /**
         * @brief Appends a basic block to the current context's linked list of blocks.
         *
         * This method adds a new basic block to the existing sequence of blocks in the
         * intermediate representation context. If no entry block is defined in the current
         * context, the provided block will be set as the entry and current block. Otherwise,
         * the block is appended to the end of the current block sequence.
         *
         * @param bb The basic block to be appended. Must not be a null pointer.
         */
        void AppendBasicBlock(BasicBlock *bb) const noexcept;

        /**
         * @brief Deletes a basic block from the intermediate representation and deallocates its memory.
         *
         * This method removes the specified basic block from the internal object list, invokes its destructor,
         * and releases the associated memory using the allocator.
         *
         * @param bb A pointer to the basic block to be deleted. Must be a valid instance managed by the Builder.
         */
        void DeleteBasicBlock(BasicBlock *bb) const noexcept;

        /**
         * @brief Exits the current compilation context and computing liveness.
         */
        void LeaveContext();
    };
}

#endif // !ORBIT_LIFTOFF_IR_BUILDER_H_
