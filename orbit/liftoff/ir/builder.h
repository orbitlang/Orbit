// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_BUILDER_H_
#define ORBIT_LIFTOFF_IR_BUILDER_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/liftoff/ir/basicblock.h>

namespace liftoff::ir {
    class Builder {
        orbiter::IsolateAllocator allocator_;

        BasicBlock *block_ = nullptr;
    public:
        explicit Builder(orbiter::Isolate *isolate) : allocator_(isolate) {};

        Object *CreateBinaryOp(orbiter::OPCode opcode, Object *left, Object *right);

        Instruction *LoadFromStackOffset(unsigned short offset);
    };
}

#endif // !ORBIT_LIFTOFF_IR_BUILDER_H_