// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_BUILDER_H_
#define ORBIT_LIFTOFF_IR_BUILDER_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/liftoff/ir/ircontext.h>
#include <orbit/liftoff/ir/module.h>

#include "value.h"

namespace liftoff::ir {
    class Builder {
        orbiter::IsolateAllocator allocator_;

        IRContext *context_ = nullptr;

        BasicBlock *AddInstruction(Instruction *instruction) noexcept;

    public:
        explicit Builder(orbiter::Isolate *isolate) noexcept: allocator_(isolate) {
        };

        Instruction *CreateBinaryOp(orbiter::OPCode opcode, Object *left, Object *right) noexcept;

        Instruction *CreateBinaryOpFlags(orbiter::OPCode opcode, U8 flags, Object *left, Object *right) noexcept;

        Instruction *LoadFromStackOffset(U16 offset) noexcept;

        Instruction *LoadImmediate(MachineSize value) noexcept;

        Module *CreateModule() noexcept;

        Value *CreateImmediateValue(U16 value) noexcept;
    };
}

#endif // !ORBIT_LIFTOFF_IR_BUILDER_H_
