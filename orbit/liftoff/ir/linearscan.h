// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_LINEARSCAN_H_
#define ORBIT_LIFTOFF_IR_LINEARSCAN_H_

#include <set>

#include <orbit/liftoff/ir/builder.h>
#include <orbit/liftoff/ir/ircontext.h>

namespace liftoff::ir {
    class LinearScan {
        struct IntervalEndComparator {
            bool operator()(const LiveInterval *a, const LiveInterval *b) const {
                return a->end < b->end;
            }
        };

        std::set<LiveInterval *, IntervalEndComparator> active_;
        std::set<LiveInterval *> active_stack_;

        std::set<U16> free_registers_;
        std::set<U16> free_stack_slot_;

        Builder builder_;

        IRContext *ir_;

        U16 stack_offset_;

        const U16 total_regs_;

        U16 GetFreeStackSlot();

        void AllocateSpecificRegister(LiveInterval &interval);

        void ExpireOldIntervals(U32 position);

        /**
         * Resolves instruction interferences in the Intermediate Representation (IR) of a program.
         *
         * This method performs two main tasks:
         * 1. Assigns unique offsets to each instruction in a linear sequence.
         * 2. Detects and handles situations where an instruction uses an operand before a function call
         *    or context switch. If such a scenario is identified, the operand's current value is saved
         *    to the stack before the function call and restored afterwards. This ensures that any
         *    modifications to the register caused by the function call do not affect the operand's
         *    correctness.
         *
         * Detailed behavior:
         * - Iterates through all basic blocks and their respective instructions.
         * - Updates the `instr_offset` field to establish a logical ordering of instructions.
         * - Identifies operands accessed before a function call or context switch. If the operand's data
         *   would be overwritten by such an event, a store operation is inserted before the event, and
         *   the value is later restored with a load operation before subsequent use.
         * - Adjusts logical offsets for instructions when interference points are identified and resolved.
         *
         * Special consideration is given to instructions such as function calls or subroutine call
         * opcodes (`CALL` and `EXECSUB`), which mark interference points requiring operand preservation.
         */
        void ResolveInterferences();

        void SpillAndAssignRegister(LiveInterval *interval);

        void SpillToStackAndReloadUses(Instruction *instruction);

    public:
        explicit LinearScan(IRContext *ir, U16 total_regs) noexcept;

        void Allocate();
    };
}

#endif // !ORBIT_LIFTOFF_IR_LINEARSCAN_H_
