// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_INSTRUCTION_H_
#define ORBIT_LIFTOFF_IR_INSTRUCTION_H_

#include <cassert>

#include <orbit/orbiter/opcode.h>

#include <orbit/liftoff/ir/object.h>
#include <orbit/liftoff/ir/register.h>

namespace liftoff::ir {
    using MachineSize = PtrSize;

    class BasicBlock;

    class Builder;

    class HasDestinationMixin {
    public:
        Register dest;
    };

    class Instruction : public Object {
        friend Builder;

    public:
        Instruction *next = nullptr;
        Instruction *prev = nullptr;

    protected:
        explicit Instruction(ObjectType obj_type) noexcept: Object(obj_type) {
        }
    };

    class PhysInstruction : public Instruction {
        friend Builder;

    protected:
        orbiter::OPCode opcode{};

        explicit PhysInstruction(orbiter::OPCode opcode) noexcept: Instruction(ObjectType::INSTRUCTION),
                                                                   opcode(opcode) {
        }
    };

    class VirtualInstruction : public Instruction, public HasDestinationMixin {
        friend Builder;

    protected:
        explicit VirtualInstruction() noexcept: Instruction(ObjectType::VIRT_INSTRUCTION) {
        }
    };

    // *****************************************************************************************************************
    // DefInstruction
    // *****************************************************************************************************************

    class DefInstruction : public PhysInstruction, public HasDestinationMixin {
        friend Builder;

    protected:
        explicit DefInstruction(orbiter::OPCode opcode) noexcept: PhysInstruction(opcode) {
        }
    };

    // BinaryOp
    class BinaryOpInstr : public DefInstruction {
        friend Builder;

    public:
        Object *left = nullptr;
        Object *right = nullptr;

    protected:
        explicit BinaryOpInstr(orbiter::OPCode opcode) noexcept: DefInstruction(opcode) {
        }
    };

    class BinaryOpFlagsInstr : public BinaryOpInstr {
        friend Builder;

    public:
        U8 flags;

    protected:
        explicit BinaryOpFlagsInstr(orbiter::OPCode opcode, U8 flags) noexcept: BinaryOpInstr(opcode), flags(flags) {
        }
    };

    // Branch
    class BranchInstruction : public PhysInstruction {
        friend Builder;

    public:
        Object *value = nullptr;

        BasicBlock *jmp = nullptr;

    protected:
        explicit BranchInstruction(orbiter::OPCode opcode, Object *value,
                                   BasicBlock *jmp) noexcept: PhysInstruction(opcode), value(value), jmp(jmp) {
        }
    };

    // Load/Store
    class LoadImmValueInstr : public DefInstruction {
        friend Builder;

    public:
        MachineSize value = 0;

    protected:
        explicit LoadImmValueInstr(MachineSize value) noexcept: DefInstruction(orbiter::OPCode::LDIMM), value(value) {
        }
    };

    class LoadStoreWithOffsetInstr : public DefInstruction {
        friend Builder;

    public:
        U16 offset = 0;

    protected:
        explicit LoadStoreWithOffsetInstr(orbiter::OPCode opcode, U16 offset) noexcept: DefInstruction(opcode),
            offset(offset) {
        }
    };

    // Phi(φ) virtual instruction
    class PhiInstr : public VirtualInstruction {
        // NOTE: Currently, only two instructions can be allocated,
        // useful for example for the ternary operator or null coalescing.
        // If necessary, it will be implemented through a vector

        Instruction *targets_[2]{};
        short index = 0;

        friend Builder;

    public:
        PhiInstr *AddTarget(Instruction *target) {
            assert(this->index <=2);

            this->targets_[this->index++] = target;

            return this;
        }
    };

    // UnaryOp
    class UnaryOpInstr : public DefInstruction {
        friend Builder;

    public:
        Object *s_reg = nullptr;

    protected:
        explicit UnaryOpInstr(orbiter::OPCode opcode) noexcept: DefInstruction(opcode) {
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_INSTRUCTION_H_
