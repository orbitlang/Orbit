// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_BASICBLOCK_H_
#define ORBIT_LIFTOFF_IR_BASICBLOCK_H_

#include <orbit/liftoff/ir/instruction.h>

namespace liftoff::ir {
    class BasicBlock : public Object {
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

        [[nodiscard]] bool IsInstructionListEmpty() const noexcept {
            return instr.head == nullptr;
        }

        void AddInstruction(Instruction *instr) noexcept {
            if (this->instr.head == nullptr) {
                this->instr.head = instr;
                this->instr.tail = instr;

                return;
            }

            instr->prev = this->instr.tail;
            this->instr.tail->next = instr;
            this->instr.tail = instr;
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_BASICBLOCK_H_
