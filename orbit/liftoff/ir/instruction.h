// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_INSTRUCTION_H_
#define ORBIT_LIFTOFF_IR_INSTRUCTION_H_

#include <cassert>

#include <orbit/orbiter/opcode.h>

#include <orbit/liftoff/ir/object.h>

namespace liftoff::ir {
    using MachineSize = PtrSize;

    constexpr auto kCallNArgsReg = 10;
    constexpr auto kCallRestReg = 11;
    constexpr auto kCallKWArgsReg = 12;
    constexpr auto kReturnRegisterReg = 13;
    constexpr auto kBaseStackPointerReg = 14;
    constexpr auto kStackPointerReg = 15;

    constexpr auto kUninitializedReg = -1;
    constexpr auto kDoNotAllocateReg = -2;

    class BasicBlock;

    class Builder;

    class Instruction : public Object {
        friend Builder;

    protected:
        explicit Instruction(const ObjectType type, const unsigned short operands) noexcept : Object(type, operands) {
        }

        explicit Instruction(const unsigned short operands) noexcept : Instruction(ObjectType::INSTRUCTION, operands) {
        }

    public:
        BasicBlock *basic_block = nullptr;

        Instruction *next = nullptr;
        Instruction *prev = nullptr;

        U32 instr_offset = 0;
        U32 intf_point = 0;

        I16 assigned_reg = kUninitializedReg;
        I16 stack_slot = -1;

        virtual void SetRegister(const I16 reg) noexcept {
            this->assigned_reg = reg;
        }
    };

    class PhysInstruction : public Instruction {
        friend Builder;

    protected:
        explicit PhysInstruction(const orbiter::OPCode opcode) noexcept : Instruction(0), opcode(opcode) {
        }

        explicit PhysInstruction(const orbiter::OPCode opcode,
                                 const unsigned int operands) noexcept : Instruction(operands), opcode(opcode) {
        }

    public:
        orbiter::OPCode opcode{};
    };

    // Error instr
    class ErrorInstr final : public PhysInstruction {
        friend Builder;

    protected:
        explicit ErrorInstr(Instruction *kind, Instruction *reason, Instruction *details) noexcept : PhysInstruction(
            orbiter::OPCode::NERROR, 3) {
            this->SetOperand(0, kind);
            this->SetOperand(1, reason);
            this->SetOperand(2, details);
        }
    };

    // BinaryOp
    class BinaryOpInstr : public PhysInstruction {
        friend Builder;
        friend class BinaryOpImmInstr;

        explicit BinaryOpInstr(const orbiter::OPCode opcode, const U8 flags) noexcept : PhysInstruction(opcode, 1),
            flags(flags) {
        }

    protected:
        explicit BinaryOpInstr(const orbiter::OPCode opcode, const U8 flags, Instruction *left,
                               Instruction *right) noexcept : PhysInstruction(opcode, 2), flags(flags) {
            this->SetOperand(0, left);
            this->SetOperand(1, right);
        }

        explicit BinaryOpInstr(const orbiter::OPCode opcode, Instruction *left,
                               Instruction *right) noexcept : BinaryOpInstr(
            opcode, 0, left, right) {
        }

    public:
        U8 flags = 0;
    };

    class BinaryOpImmInstr final : public BinaryOpInstr {
        friend Builder;

    protected:
        explicit BinaryOpImmInstr(const orbiter::OPCode opcode, const U8 flags, Instruction *left,
                                  const U16 right) noexcept : BinaryOpInstr(opcode, flags), imm(right) {
            this->SetOperand(0, left);
        }

    public:
        U16 imm = 0;
    };

    // TernaryOp
    class TernaryOpImmInstr final : public PhysInstruction {
        friend Builder;

    protected:
        explicit TernaryOpImmInstr(const orbiter::OPCode opcode, Instruction *dst, Instruction *l_src,
                                   Instruction *d_src, const U8 flags) noexcept : PhysInstruction(opcode, 3),
            flags(flags) {
            if (opcode == orbiter::OPCode::PUSHIF)
                this->assigned_reg = kDoNotAllocateReg;

            this->SetOperand(0, dst);
            this->SetOperand(1, l_src);
            this->SetOperand(2, d_src);
        }

    public:
        U8 flags = 0;
    };

    // Branch
    class BranchInstruction final : public PhysInstruction {
        friend Builder;

    protected:
        explicit BranchInstruction(const orbiter::OPCode opcode, Instruction *value,
                                   BasicBlock *jmp) noexcept : PhysInstruction(opcode, 2) {
            if (opcode == orbiter::OPCode::ITRNXT)
                this->assigned_reg = kReturnRegisterReg;

            this->SetOperand(0, value);
            this->SetOperand(1, (Object *) jmp);
        }

    public:
        void SetBasicBlock(BasicBlock *jmp) const {
            this->SetOperand(1, (Object *) jmp);
        }
    };

    class CallInstr final : public PhysInstruction {
        friend Builder;

    public:
        U16 arguments = 0;
        orbiter::CallMode mode;

        void SetKwargs(Instruction *instr) const {
            instr->assigned_reg = kCallKWArgsReg;

            this->SetOperand(3, instr);
        }

        void SetNargs(Instruction *instr) const {
            instr->assigned_reg = kCallNArgsReg;

            this->SetOperand(1, instr);
        }

        void SetRest(Instruction *instr) const {
            instr->assigned_reg = kCallRestReg;

            this->SetOperand(2, instr);
        }

    protected:
        explicit CallInstr(const orbiter::OPCode opcode, Instruction *src, const U16 arguments,
                           const orbiter::CallMode mode) noexcept : PhysInstruction(opcode, 4),
                                                                    arguments(arguments), mode(mode) {
            this->SetOperand(0, src);

            this->assigned_reg = kReturnRegisterReg;
        }

        explicit CallInstr(const orbiter::OPCode opcode, Instruction *src,
                           const U16 arguments) noexcept : PhysInstruction(opcode, 1),
                                                           arguments(arguments), mode(orbiter::CallMode::FASTCALL) {
            this->SetOperand(0, src);

            this->assigned_reg = kReturnRegisterReg;
        }
    };

    class ExecSubInstr final : public PhysInstruction {
        friend Builder;

    protected:
        explicit ExecSubInstr(Instruction *src) noexcept : PhysInstruction(
            orbiter::OPCode::EXECSUB, 1) {
            this->SetOperand(0, src);

            this->assigned_reg = kReturnRegisterReg;
        }
    };

    class PendingActionInstruction final : public PhysInstruction {
        friend Builder;

    protected:
        explicit PendingActionInstruction(BasicBlock *jmp,
                                          const orbiter::PendingAction action) noexcept : PhysInstruction(
                orbiter::OPCode::TSPA, 2), action(action) {
            assert(action != orbiter::PendingAction::RETURN);

            this->SetOperand(1, (Object *) jmp);
        }

        explicit PendingActionInstruction(Instruction *value, const U16 pops) noexcept : PhysInstruction(
                orbiter::OPCode::TSPA, 2), action(orbiter::PendingAction::RETURN), pops(pops) {
            this->SetOperand(0, value);
        }

    public:
        orbiter::PendingAction action;
        U16 pops = 0;
    };

    // Load/Store
    class LoadFunc final : public PhysInstruction {
        friend Builder;

    protected:
        explicit LoadFunc(Instruction *src, Instruction *def_args,
                          orbiter::LoadFuncFlags flags) noexcept : PhysInstruction(orbiter::OPCode::LDFUNC, 2),
                                                                   flags((U16) flags) {
            this->SetOperand(0, src);
            this->SetOperand(1, def_args);
        }

    public:
        U16 flags = 0;
    };

    class LoadImmValueInstr final : public PhysInstruction {
        friend Builder;

    public:
        MachineSize value = 0;

    protected:
        explicit LoadImmValueInstr(const MachineSize value) noexcept : PhysInstruction(orbiter::OPCode::LDIMM),
                                                                       value(value) {
        }
    };

    class LSObjectProp final : public PhysInstruction {
        friend Builder;

    public:
        orbiter::LoadObjectPropFlags flags;
        U16 offset;

        LSObjectProp(const orbiter::OPCode opcode, Instruction *object, const U16 offset,
                     const orbiter::LoadObjectPropFlags flags) noexcept : PhysInstruction(opcode, 1), flags(flags),
                                                                          offset(offset) {
            this->SetOperand(0, object);
        }

        LSObjectProp(const orbiter::OPCode opcode, Instruction *object, Instruction *value, const U16 offset,
                     const orbiter::LoadObjectPropFlags flags) noexcept : PhysInstruction(opcode, 2), flags(flags),
                                                                          offset(offset) {
            this->SetOperand(0, object);
            this->SetOperand(1, value);
        }
    };

    class ManipInstruction final : public PhysInstruction {
        friend Builder;

    public:
        ManipInstruction(const orbiter::OPCode opcode, Instruction *target, Instruction *src,
                         Instruction *src1) noexcept : PhysInstruction(opcode, 3) {
            this->SetOperand(0, target);
            this->SetOperand(1, src);
            this->SetOperand(2, src1);

            this->assigned_reg = kDoNotAllocateReg;
        }

        ManipInstruction(const orbiter::OPCode opcode, Instruction *target,
                         Instruction *src) noexcept : PhysInstruction(
            opcode, 2) {
            this->SetOperand(0, target);
            this->SetOperand(1, src);

            this->assigned_reg = kDoNotAllocateReg;
        }
    };

    class ManipTypeInstruction final : public PhysInstruction {
        friend Builder;

    public:
        U16 offset = 0;

        ManipTypeInstruction(const orbiter::OPCode opcode, Instruction *target, Instruction *src,
                             const U16 offset) noexcept : PhysInstruction(opcode, 2),
                                                          offset(offset) {
            this->SetOperand(0, target);
            this->SetOperand(1, src);
        }
    };

    class OffsetInstruction final : public PhysInstruction {
        friend Builder;

    public:
        U8 flags = 0;
        U8 r_base = 0;
        I16 offset = 0;

        OffsetInstruction(const orbiter::OPCode opcode, const I16 offset) noexcept : PhysInstruction(opcode),
            offset(offset) {
        }

        OffsetInstruction(const orbiter::OPCode opcode, const U8 r_base,
                          const I16 offset) noexcept : PhysInstruction(opcode),
                                                       r_base(r_base), offset(offset) {
        }

        OffsetInstruction(const orbiter::OPCode opcode, const U8 r_base, const I16 offset,
                          Instruction *src) noexcept : PhysInstruction(opcode, 1),
                                                       r_base(r_base), offset(offset) {
            this->SetOperand(0, src);
        }

        OffsetInstruction(const orbiter::OPCode opcode, const I16 offset,
                          Instruction *src) noexcept : PhysInstruction(opcode, 1), offset(offset) {
            this->SetOperand(0, src);
        }
    };

    class ReturnInstruction final : PhysInstruction {
        friend Builder;

    protected:
        explicit ReturnInstruction(Instruction *instr, const U16 slots) : PhysInstruction(orbiter::OPCode::RET, 1),
                                                                          slots(slots) {
            this->SetOperand(0, instr);
        }

    public:
        U16 slots = 0;
    };

    class ReturnSubInstruction final : PhysInstruction {
        friend Builder;

    protected:
        explicit ReturnSubInstruction(Instruction *instr) : PhysInstruction(orbiter::OPCode::RETSUB, 1) {
            this->SetOperand(0, instr);
        }
    };

    class SubscrInstruction final : PhysInstruction {
        friend Builder;

    public:
        SubscrInstruction(const orbiter::OPCode opcode, Instruction *target, Instruction *index) : PhysInstruction(
            opcode, 2) {
            this->SetOperand(0, target);
            this->SetOperand(1, index);
        }

        SubscrInstruction(const orbiter::OPCode opcode, Instruction *target, Instruction *index,
                          Instruction *value) : PhysInstruction(opcode, 3) {
            this->SetOperand(0, target);
            this->SetOperand(1, index);
            this->SetOperand(2, value);
        }

        SubscrInstruction(const orbiter::OPCode opcode, Instruction *target, Instruction *start, Instruction *stop,
                          Instruction *step) : PhysInstruction(opcode, 4) {
            this->SetOperand(0, target);
            this->SetOperand(1, start);
            this->SetOperand(2, stop);
            this->SetOperand(3, step);
        }

        SubscrInstruction(const orbiter::OPCode opcode, Instruction *target, Instruction *start, Instruction *stop,
                          Instruction *step, Instruction *value) : PhysInstruction(opcode, 5) {
            this->SetOperand(0, target);
            this->SetOperand(1, start);
            this->SetOperand(2, stop);
            this->SetOperand(3, step);
            this->SetOperand(4, value);
        }
    };

    // UnaryOp immediate
    class UnaryImmInstr final : public PhysInstruction {
        friend Builder;

    public:
        U8 flags = 0;
        U16 imm = 0;

    protected:
        explicit UnaryImmInstr(const orbiter::OPCode opcode, const U8 flags,
                               const U16 imm) noexcept : PhysInstruction(opcode),
                                                         flags(flags), imm(imm) {
        }

        explicit UnaryImmInstr(const orbiter::OPCode opcode, const U8 flags) noexcept : PhysInstruction(opcode),
            flags(flags) {
        }
    };

    // UnaryOp
    class UnaryOpInstr final : public PhysInstruction {
        friend Builder;

    protected:
        explicit UnaryOpInstr(const orbiter::OPCode opcode) noexcept : PhysInstruction(opcode) {
        }

        explicit UnaryOpInstr(const orbiter::OPCode opcode, Instruction *src) noexcept : UnaryOpInstr(opcode, 0, src) {
        }

        explicit UnaryOpInstr(const orbiter::OPCode opcode, const U8 flags,
                              Instruction *src) noexcept : PhysInstruction(opcode, 1), flags(flags) {
            this->SetOperand(0, src);
        }

    public:
        U8 flags = 0;
    };

    // *****************************************************************************************************************
    // Virtual Instruction
    // *****************************************************************************************************************

    class VirtualInstruction : public Instruction {
        friend Builder;

    protected:
        explicit VirtualInstruction() noexcept : Instruction(ObjectType::VIRT_INSTRUCTION, 0) {
        }

        explicit VirtualInstruction(const int operands) noexcept : Instruction(ObjectType::VIRT_INSTRUCTION, operands) {
        }
    };

    // Phi(φ) virtual instruction
    class PhiInstr final : public VirtualInstruction {
        // NOTE: Currently, only two instructions can be allocated,
        // useful for example for the ternary operator or null coalescing.

        short index = 0;

        friend Builder;

    public:
        PhiInstr() noexcept : VirtualInstruction(2) {
        }

        PhiInstr *AddTarget(Instruction *src) noexcept {
            assert(this->index<2);

            src->assigned_reg = kDoNotAllocateReg;

            this->SetOperand(this->index++, src);

            return this;
        }

        void SetRegister(const I16 reg) noexcept override {
            this->assigned_reg = reg;

            for (auto i = 0; i < this->index; i++)
                ((Instruction *) this->operands[i].value)->assigned_reg = reg;
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_INSTRUCTION_H_
