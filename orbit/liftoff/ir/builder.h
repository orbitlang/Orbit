// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_BUILDER_H_
#define ORBIT_LIFTOFF_IR_BUILDER_H_

#include <orbit/orbiter/memory/iallocator.h>

#include <orbit/liftoff/ir/ircontext.h>

namespace liftoff::ir {
    /**
     * This constant is paired with the one in vm.cpp, and they must always have the same size.
     * The vm.cpp value is in bytes while this one is in slots, where: slots * sizeof(void*) = bytes
     */
    constexpr auto kStackPrologueOffset = 4 + 2; // FiberContext + BP + IP

    /**
     * @brief Builder class for constructing Intermediate Representation (IR) instructions and basic blocks.
     *
     * The Builder class provides methods to create, append, and manipulate instructions, objects,
     * and basic blocks within an intermediate representation context. It facilitates the construction
     * of an IR for code generation purposes and supports functionalities such as handling stack
     * operations, constants, and various opcodes.
     */
    class Builder {
        orbiter::memory::IsolateAllocator allocator_;

        bool delete_context_ = true;

        /** @brief Creates an object of a specified type and adds it to the object list.
         *
         * @tparam T Type of the object to create. Must be derived from Object.
         * @tparam Args Variadic template arguments for the object's constructor.
         * @param args Constructor arguments to initialize the object.
         *
         * @return Pointer to the created object.
         * @throws std::bad_alloc If memory allocation fails.
         */
        template<typename T, typename... Args>
        std::enable_if_t<std::is_base_of_v<Object, T>, T *> CreateObject(Args... args) {
            auto *obj = this->allocator_.alloc<T>(sizeof(T));
            if (obj == nullptr)
                throw std::bad_alloc();

            new(obj)T(args...);

            this->context->Add2ObjList(obj);

            return obj;
        }

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
        template<typename T, typename... Args>
        std::enable_if_t<std::is_base_of_v<Instruction, T>, T *> CreateInstruction(Args... args) {
            auto *instr = this->CreateObject<T>(args...);

            this->AddInstruction(instr);

            return instr;
        }

        /**
         * @brief Retrieves the last instruction with the specified opcode.
         *
         * This method searches the current basic block for the last instruction that matches
         * the given opcode and returns it if found. If no matching instruction exists or
         * the current context is null, it returns a null pointer.
         *
         * @param opcode The opcode to match while searching for the instruction.
         * @return A pointer to the last instruction matching the specified opcode,
         *         or null if no such instruction exists.
         */
        [[nodiscard]] const PhysInstruction *GetLastInstructionMatch(orbiter::OPCode opcode) const noexcept;

    public:
        IRContext *context = nullptr;

        /**
         * @brief Constructs a new Builder object.
         *
         * @param isolate Pointer to isolate.
         */
        explicit Builder(orbiter::Isolate *isolate) noexcept: allocator_(isolate) {
        }

        explicit Builder(IRContext *ir) noexcept: allocator_(ir->GetIsolate()),
                                                  delete_context_(false),
                                                  context(ir) {
        }

        ~Builder() noexcept;

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

        [[nodiscard]] bool CheckIfLastInstructionIs(orbiter::OPCode opcode) const;

        Instruction *AllocStackSlots(U16 slots, orbiter::AllocaFlags flags);

        Instruction *CreateBinaryOp(orbiter::OPCode opcode, Instruction *left, Instruction *right);

        Instruction *CreateBinaryOpFlags(orbiter::OPCode opcode, U8 flags, Instruction *left, Instruction *right);

        Instruction *CreateBinaryOpFlags(orbiter::OPCode opcode, U8 flags, Instruction *left, U16 right);

        Instruction *CreateBranch(orbiter::OPCode opcode, Instruction *value, BasicBlock *continuation,
                                  BasicBlock *destination);

        Instruction *CreateCall(Instruction *src, U16 arguments, orbiter::CallMode mode);

        Instruction *CreateCallDetached(orbiter::OPCode opcode, Instruction *src, U16 arguments, orbiter::CallMode mode);

        Instruction *CreateJump(BasicBlock *destination);

        Instruction *CreateManip(orbiter::OPCode opcode, Instruction *target, Instruction *src, Instruction *src1);

        Instruction *CreateManip(orbiter::OPCode opcode, Instruction *target, Instruction *src);

        Instruction *CreateManipType(orbiter::OPCode opcode, Instruction *target, Instruction *src, U16 offset);

        Instruction *CreateStoreVariable(orbiter::OPCode opcode, I16 offset, U8 flags, Instruction *value);

        Instruction *CreateReturn(Instruction *s_reg, U16 slots);

        Instruction *CreateReturn(U16 slots);

        Instruction *CreateReturnSub(Instruction *s_reg);

        Instruction *CreateUnaryOp(orbiter::OPCode opcode, Instruction *s_reg);

        Instruction *CreateUnaryOp(orbiter::OPCode opcode, U16 imm, U8 flags);

        Instruction *CreateUnaryOp(const orbiter::OPCode opcode, const U16 imm) {
            return this->CreateUnaryOp(opcode, imm, 0);
        }

        Instruction *CreateUnaryOp(const orbiter::OPCode opcode) {
            return this->CreateUnaryOp(opcode, 0, 0);
        }

        Instruction *LoadCodeObject(U16 offset);

        Instruction *LoadLastCodeObject() {
            return this->LoadCodeObject(this->context->GetSubcontextCount() - 1);
        }

        Instruction *LoadConstant(U16 offset);

        Instruction *LoadConstant(orbiter::datatype::OObject *object);

        Instruction *LoadClosureObject(U8 r_base, I16 offset);

        Instruction *LoadExecCodeObject(U16 offset);

        Instruction *LoadExecLastCodeObject() {
            return this->LoadExecCodeObject(this->context->GetSubcontextCount() - 1);
        }

        Instruction *LoadFalseValue() {
            return this->CreateInstruction<
                UnaryImmInstr>(orbiter::OPCode::LDCST, (U8) orbiter::LoadConstantMode::FALSE);
        }

        Instruction *LoadFromClosureAtOffset(I16 offset);

        Instruction *LoadFromStackOffset(U8 r_base, I16 offset, bool force_load);

        Instruction *LoadFunction(Instruction *src, Instruction *def_args, orbiter::LoadFuncFlags flags);

        Instruction *LoadImmediate(MachineSize value);

        Instruction *LoadNilValue() {
            return this->CreateInstruction<UnaryImmInstr>(orbiter::OPCode::LDCST, (U8) orbiter::LoadConstantMode::NIL);
        }

        Instruction *LoadObjectProp(Instruction *src, U16 offset, bool as_key, bool super);

        /**
         * @brief Loads or stores using the specified opcode and offset.
         *
         * @param opcode The opcode for the load/store operation.
         * @param offset The offset for the load/store operation.
         * @param flags flags
         *
         * @return Pointer to the created instruction.
         */
        Instruction *LoadFromOffset(orbiter::OPCode opcode, U8 r_basem, I16 offset, U8 flags);

        Instruction *LoadFromOffset(orbiter::OPCode opcode, I16 offset, U8 flags) {
            return this->LoadFromOffset(opcode, 0, offset, flags);
        }

        Instruction *LoadTrueValue() {
            return this->CreateInstruction<UnaryImmInstr>(orbiter::OPCode::LDCST, (U8) orbiter::LoadConstantMode::TRUE);
        }

        Instruction *GetLoadFromStackOffset(U8 r_base, I16 offset) {
            this->context->program_size += 4;

            return this->CreateObject<OffsetInstruction>(orbiter::OPCode::SKLDR, r_base, offset);
        }

        Instruction *GetStoreToStackOffset(Instruction *src, U8 r_base, I16 offset) {
            this->context->program_size += 4;

            return this->CreateObject<OffsetInstruction>(orbiter::OPCode::SKSTR, r_base, offset, src);
        }

        Instruction *StackDiscard(U16 slots);

        Instruction *GetStackPop();

        Instruction *StackPop();

        Instruction *GetStackPush(Instruction *s_reg);

        Instruction *StackPush(Instruction *s_reg);

        Instruction *GetStoreObjectProp(Instruction *obj, Instruction *value, U16 offset, bool as_key);

        Instruction *StoreObjectProp(Instruction *obj, Instruction *value, U16 offset, bool as_key);

        Instruction *StoreToClosureAtOffset(Instruction *src, I16 offset);

        Instruction *StoreToStackOffset(Instruction *src, U8 r_base, I16 offset);

        PhiInstr *CreatePhi();

        /**
         * @brief Adds an instruction to the current basic block.
         *
         * @param instruction The instruction to add.
         *
         * @return Pointer to the basic block.
         */
        BasicBlock *AddInstruction(Instruction *instruction);

        /**
         * @brief Creates a new intermediate representation (IR) context of the specified type.
         *
         * The method allocates memory for a new IR context, initializes it with the provided type,
         * handles it within the current context stack, and invalidates active variables
         * before associating the new context as the current context.
         *
         * @param type The type of the IR context to be created.
         * @param local_slots The number of local slots available for the IR context.
         *
         * @return The identifier of the new IR context as a U16 type.
         *
         * @throws std::bad_alloc if memory allocation for the new IR context fails.
         * @throws Any exception propagated from context management operations.
         */
        U16 IRContextNew(IRContextType type, U16 local_slots);

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
