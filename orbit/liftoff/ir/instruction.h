// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_INSTRUCTION_H_
#define ORBIT_LIFTOFF_IR_INSTRUCTION_H_

#include <orbit/orbiter/opcode.h>

#include <orbit/liftoff/ir/object.h>
#include <orbit/liftoff/ir/register.h>

namespace liftoff::ir {
    class Instruction : public Object {
    public:
        orbiter::OPCode opcode;

        Instruction *next = nullptr;
        Instruction *prev = nullptr;

        explicit Instruction(orbiter::OPCode opcode) noexcept: Object(ObjectType::INSTRUCTION), opcode(opcode) {
        }
    };

    class DefInstruction : public Instruction {
    public:
        Register dest;

        explicit DefInstruction(orbiter::OPCode opcode) noexcept: Instruction(opcode) {
        };
    };

    class BinaryOpInstr : public DefInstruction {
    public:
        Object *left = nullptr;
        Object *right = nullptr;

        explicit BinaryOpInstr(orbiter::OPCode opcode) noexcept: DefInstruction(opcode) {
        };
    };

    class StackLoadInstr : public DefInstruction {
    public:
        U16 offset = 0;

        explicit StackLoadInstr() noexcept: DefInstruction(orbiter::OPCode::SKLDR) {
        };
    };
}

#endif // !ORBIT_LIFTOFF_IR_INSTRUCTION_H_
