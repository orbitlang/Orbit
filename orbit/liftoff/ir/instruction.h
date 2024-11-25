// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_INSTRUCTION_H_
#define ORBIT_LIFTOFF_IR_INSTRUCTION_H_

#include <orbit/orbiter/opcode.h>

#include <orbit/liftoff/ir/object.h>
#include <orbit/liftoff/ir/register.h>

namespace liftoff::ir {
    using MachineSize = PtrSize;

    class BasicBlock;

    class Builder;

    class Instruction : public Object {
        friend Builder;

    public:
        orbiter::OPCode opcode;

        Instruction *next = nullptr;
        Instruction *prev = nullptr;

    protected:
        explicit Instruction(orbiter::OPCode opcode) noexcept: Object(ObjectType::INSTRUCTION), opcode(opcode) {
        }
    };

    // *****************************************************************************************************************
    // DefInstruction
    // *****************************************************************************************************************

    class DefInstruction : public Instruction {
        friend Builder;

    public:
        Register dest;

    protected:
        explicit DefInstruction(orbiter::OPCode opcode) noexcept: Instruction(opcode) {
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
    class BranchInstruction : public Instruction {
        friend Builder;

    public:
        BasicBlock *jmp = nullptr;

    protected:
        explicit BranchInstruction(orbiter::OPCode opcode, BasicBlock *jmp) noexcept: Instruction(opcode), jmp(jmp) {
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
