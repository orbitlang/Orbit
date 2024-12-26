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

    class BasicBlock;

    class Builder;

    class Use {
    public:
        class Instruction *value = nullptr;
        Instruction *user = nullptr;

        int index = 0;

        Use *next = nullptr;
    };

    class Instruction : public Object {
        friend Builder;

        void AddUse(Use *u) noexcept {
            u->next = this->use_list;
            this->use_list = u;
        }

        void DeleteUse(Use *u) noexcept {
            if (this->use_list == u) {
                this->use_list = nullptr;

                return;
            }

            auto *prev = this->use_list;
            for (auto cur = this->use_list->next; cur != nullptr; cur = cur->next) {
                if (cur == u) {
                    prev->next = cur->next;

                    break;
                }

                prev = cur;
            }
        }

    protected:
        explicit Instruction(ObjectType type, int operands) noexcept: Object(type), num_ops(operands) {
            if (operands > 0) {
                this->operands = new Use[operands];

                for (int i = 0; i < num_ops; ++i) {
                    this->operands[i].user = this;
                    this->operands[i].index = i;
                }
            }
        }

        explicit Instruction(int operands) noexcept: Instruction(ObjectType::INSTRUCTION, operands) {
        }

        void SetOperand(int operand, Instruction *instr) noexcept {
            if (this->operands[operand].value != nullptr)
                this->DeleteUse(this->operands + operand);

            this->operands[operand].value = instr;
            instr->AddUse(this->operands + operand);
        }

    public:
        Use *operands = nullptr;
        Use *use_list = nullptr;

        Instruction *next = nullptr;
        Instruction *prev = nullptr;

        U32 offset = 0;

        const U32 num_ops = 0;

        virtual ~Instruction() {
            delete[] this->operands;
        }
    };

    class PhysInstruction : public Instruction {
        friend Builder;

    protected:
        orbiter::OPCode opcode{};

        explicit PhysInstruction(orbiter::OPCode opcode) noexcept: Instruction(0), opcode(opcode) {
        }

        explicit PhysInstruction(orbiter::OPCode opcode, int operands) noexcept: Instruction(operands), opcode(opcode) {
        }
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

    public:
        BasicBlock *jmp = nullptr;

    protected:
        explicit BranchInstruction(orbiter::OPCode opcode, Instruction *value,
                                   BasicBlock *jmp) noexcept: PhysInstruction(opcode, 1), jmp(jmp) {
            this->SetOperand(0, value);
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

            this->SetOperand(this->index++, src);

            return this;
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_INSTRUCTION_H_
