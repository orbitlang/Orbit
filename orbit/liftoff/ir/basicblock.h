// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_BASICBLOCK_H_
#define ORBIT_LIFTOFF_IR_BASICBLOCK_H_

#include <orbit/liftoff/ir/instruction.h>

namespace liftoff::ir {
    class BasicBlock : Object {
    public:
        BasicBlock *next = nullptr;
        BasicBlock *prev = nullptr;

        struct {
            Instruction *head = nullptr;
            Instruction *tail = nullptr;
        } instr;

        U32 offset = 0;
        U32 size = 0;

        explicit BasicBlock() noexcept: Object(ObjectType::BASIC_BLOCK) {
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_BASICBLOCK_H_
