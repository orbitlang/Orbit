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

    constexpr auto kUninitializedReg = -1;
    constexpr auto kDoNotAllocateReg = -2;

    class BasicBlock;

    class Builder;

    class Instruction : public Object {
        friend Builder;

    protected:
        explicit Instruction(ObjectType type, int operands) noexcept: Object(type, operands) {
        }

        explicit Instruction(int operands) noexcept: Instruction(ObjectType::INSTRUCTION, operands) {
        }

    public:
        Instruction *next = nullptr;
        Instruction *prev = nullptr;

        U32 instr_offset = 0;

        I16 assigned_reg = kUninitializedReg;
        I16 stack_slot = -1;

        virtual void SetRegister(I16 reg) noexcept {
            this->assigned_reg = reg;
        }
    };

    class PhysInstruction : public Instruction {
        friend Builder;

    protected:
        explicit PhysInstruction(orbiter::OPCode opcode) noexcept: Instruction(0), opcode(opcode) {
        }

        explicit PhysInstruction(orbiter::OPCode opcode, int operands) noexcept: Instruction(operands), opcode(opcode) {
        }

    public:
        orbiter::OPCode opcode{};
    };

    // BinaryOp
    class BinaryOpInstr : public PhysInstruction {
        friend Builder;

    protected:
        explicit BinaryOpInstr(orbiter::OPCode opcode, U8 flags, Instruction *left,
                               Instruction *right) noexcept: PhysInstruction(opcode, 2), flags(flags) {
            this->SetOperand(0, left);
            this->SetOperand(1, right);
        }

        explicit BinaryOpInstr(orbiter::OPCode opcode, Instruction *left, Instruction *right) noexcept: BinaryOpInstr(
            opcode, 0, left, right) {
        }

    public:
        U8 flags = 0;
    };

    class BinaryOpImmInstr : public PhysInstruction {
        friend Builder;

    protected:
        explicit BinaryOpImmInstr(orbiter::OPCode opcode, U8 flags, Instruction *left,
                                  U16 right) noexcept: PhysInstruction(opcode, 1), flags(flags), imm(right) {
            this->SetOperand(0, left);
        }

    public:
        U8 flags = 0;
        U16 imm = 0;
    };

    // Branch
    class BranchInstruction : public PhysInstruction {
        friend Builder;

    protected:
        explicit BranchInstruction(orbiter::OPCode opcode, Instruction *value,
                                   BasicBlock *jmp) noexcept: PhysInstruction(opcode, 2) {
            this->SetOperand(0, value);
            this->SetOperand(1, (Object *) jmp);
        }

    public:
        void SetBasicBlock(BasicBlock *jmp) {
            this->SetOperand(1, (Object *) jmp);
        }
    };

    class CallInstr : public PhysInstruction {
        friend Builder;

    public:
        U16 arguments = 0;

    protected:
        explicit CallInstr(Instruction *src, U16 arguments) noexcept: PhysInstruction(orbiter::OPCode::CALL, 1),
                                                                      arguments(arguments) {
            this->SetOperand(0, src);
        }
    };

    class LoadStoreClosureWithOffsetInstr : public PhysInstruction {
        friend Builder;

    public:
        I16 offset;
        orbiter::ClosureLSMode mode;

    protected:
        explicit LoadStoreClosureWithOffsetInstr(orbiter::OPCode opcode, I16 offset,
                                                 orbiter::ClosureLSMode mode,
                                                 Instruction *src) noexcept: PhysInstruction(
                                                                                 opcode, 1), offset(offset),
                                                                             mode(mode) {
            this->SetOperand(0, src);
        }
    };

    class LoadImmValueInstr : public PhysInstruction {
        friend Builder;

    public:
        MachineSize value = 0;

    protected:
        explicit LoadImmValueInstr(MachineSize value) noexcept: PhysInstruction(orbiter::OPCode::LDIMM), value(value) {
        }
    };

    // Load/Store
    class OffsetInstruction : public PhysInstruction {
        friend Builder;

    public:
        U8 flags = 0;
        I16 offset = 0;

        OffsetInstruction(orbiter::OPCode opcode, I16 offset) noexcept: PhysInstruction(opcode), offset(offset) {
        }

        OffsetInstruction(orbiter::OPCode opcode, U8 flags, Instruction *src) noexcept: PhysInstruction(opcode, 1),
            flags(flags) {
            this->SetOperand(0, src);
        }

        OffsetInstruction(orbiter::OPCode opcode, I16 offset, Instruction *src) noexcept: PhysInstruction(opcode, 1),
            offset(offset) {
            this->SetOperand(0, src);
        }
    };

    class ReturnInstruction : PhysInstruction {
        friend Builder;

    protected:
        explicit ReturnInstruction(Instruction *instr, bool yield) : PhysInstruction(
            yield ? orbiter::OPCode::YLD : orbiter::OPCode::RET, 1) {
            this->SetOperand(0, instr);
        }
    };

    // UnaryOp immediate
    class UnaryImmInstr : public PhysInstruction {
        friend Builder;

    public:
        U8 flags = 0;
        U16 imm = 0;

    protected:
        explicit UnaryImmInstr(orbiter::OPCode opcode, U8 flags, U16 imm) noexcept: PhysInstruction(opcode),
            flags(flags), imm(imm) {
        }

        explicit UnaryImmInstr(orbiter::OPCode opcode, U8 flags) noexcept: PhysInstruction(opcode), flags(flags) {
        }
    };

    // UnaryOp
    class UnaryOpInstr : public PhysInstruction {
        friend Builder;

    protected:
        explicit UnaryOpInstr(orbiter::OPCode opcode) noexcept: PhysInstruction(opcode) {
        }

        explicit UnaryOpInstr(orbiter::OPCode opcode, Instruction *src) noexcept: UnaryOpInstr(opcode, 0, src) {
        }

        explicit UnaryOpInstr(orbiter::OPCode opcode, U8 flags, Instruction *src) noexcept: PhysInstruction(opcode, 1),
            flags(flags) {
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
        explicit VirtualInstruction() noexcept: Instruction(ObjectType::VIRT_INSTRUCTION, 0) {
        }

        explicit VirtualInstruction(int operands) noexcept: Instruction(ObjectType::VIRT_INSTRUCTION, operands) {
        }
    };

    // Phi(φ) virtual instruction
    class PhiInstr : public VirtualInstruction {
        // NOTE: Currently, only two instructions can be allocated,
        // useful for example for the ternary operator or null coalescing.

        short index = 0;

        friend Builder;

    public:
        PhiInstr() noexcept: VirtualInstruction(2) {
        }

        PhiInstr *AddTarget(Instruction *src) noexcept {
            assert(this->index<2);

            src->assigned_reg = kDoNotAllocateReg;

            this->SetOperand(this->index++, src);

            return this;
        }

        void SetRegister(I16 reg) noexcept override {
            this->assigned_reg = reg;

            for (auto i = 0; i < this->index; i++)
                ((Instruction *) this->operands[i].value)->assigned_reg = reg;
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_INSTRUCTION_H_
