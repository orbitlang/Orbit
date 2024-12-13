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

    class ReturnInstruction : PhysInstruction {
        friend Builder;

    public:
        Object *source = nullptr;

    protected:
        explicit ReturnInstruction(Object *source, bool yield) : PhysInstruction(
                yield
                ? orbiter::OPCode::YLD
                : orbiter::OPCode::RET), source(source) {
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
    class OffsetInstruction : public DefInstruction {
    public:
        I16 offset = 0;

        OffsetInstruction(orbiter::OPCode opcode, I16 offset) noexcept: DefInstruction(opcode), offset(offset) {
        }
    };

    class LoadCodeInstr : public OffsetInstruction {
        friend Builder;

    protected:
        explicit LoadCodeInstr(I16 offset) noexcept: OffsetInstruction(orbiter::OPCode::LDCODE, offset) {
        }
    };

    class LoadFuncInstr : public DefInstruction {
        friend Builder;

    public:
        Object *src = nullptr;

        U8 flags = 0;

    protected:
        LoadFuncInstr(Object *src, U8 flags) noexcept: DefInstruction(orbiter::OPCode::LDFUNC), src(src), flags(flags) {
        }
    };

    class LoadImmConstantInstr : public DefInstruction {
        U8 mode;

        friend Builder;

    protected:
        explicit LoadImmConstantInstr(U8 mode) : DefInstruction(orbiter::OPCode::LDCST), mode(mode) {
        }
    };

    class LoadImmValueInstr : public DefInstruction {
        friend Builder;

    public:
        MachineSize value = 0;

    protected:
        explicit LoadImmValueInstr(MachineSize value) noexcept: DefInstruction(orbiter::OPCode::LDIMM), value(value) {
        }
    };

    class LoadStoreWithOffsetInstr : public OffsetInstruction {
        friend Builder;

    public:
        Object *src = nullptr;

    protected:
        explicit LoadStoreWithOffsetInstr(orbiter::OPCode opcode, I16 offset) noexcept: OffsetInstruction(
                opcode, offset) {
        }
    };

    class LoadStoreClosureWithOffsetInstr : public OffsetInstruction {
        friend Builder;

    public:
        Object *src;

        orbiter::ClosureLSMode mode;

    protected:
        explicit LoadStoreClosureWithOffsetInstr(orbiter::OPCode opcode, I16 offset,
                                                 orbiter::ClosureLSMode mode, Object *value) noexcept: OffsetInstruction(
                opcode, offset), src(value), mode(mode) {
        }
    };

    class PopInstr : public DefInstruction {
        friend Builder;

    protected:
        PopInstr() noexcept: DefInstruction(orbiter::OPCode::POP) {
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

    class UnaryImmInstr : public DefInstruction {
        friend Builder;

    public:
        U16 imm = 0;
        U8 flags = 0;

    protected:
        explicit UnaryImmInstr(orbiter::OPCode opcode) noexcept: DefInstruction(opcode){
        }
    };

    // *****************************************************************************************************************
    // Virtual Instruction
    // *****************************************************************************************************************

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
            assert(this->index <= 2);

            this->targets_[this->index++] = target;

            return this;
        }
    };

    // *****************************************************************************************************************
    // Other
    // *****************************************************************************************************************

    class AllocaInstr : public PhysInstruction {
        friend Builder;

    public:
        orbiter::AllocaFlags flags = orbiter::AllocaFlags::DEFAULT;
        U16 slots = 0;

    protected:
        explicit AllocaInstr(U16 slots, orbiter::AllocaFlags flags) noexcept: PhysInstruction(orbiter::OPCode::ALLOCA),
                                                                              slots(slots),
                                                                              flags(flags) {
        }
    };

    class PushInstr : public PhysInstruction {
        friend Builder;

    public:
        Object *src;

    protected:
        explicit PushInstr(Object *src) noexcept: PhysInstruction(orbiter::OPCode::PUSH), src(src){
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_INSTRUCTION_H_
