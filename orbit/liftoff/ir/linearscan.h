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
                if (a->end != b->end)
                    return a->end < b->end;

                return a < b;
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

        [[nodiscard]] std::vector<U32> CallPositionPreScan() const;

        void AllocateSpecificRegister(LiveInterval &interval);

        void ExpireOldIntervals(U32 position);

        void SpillAndAssignRegister(LiveInterval *interval);

        void SpillToStackAndReloadUses(Instruction *instruction);

    public:
        explicit LinearScan(IRContext *ir, U16 total_regs) noexcept;

        void Allocate(std::vector<LiveInterval> &intervals);
    };
}

#endif // !ORBIT_LIFTOFF_IR_LINEARSCAN_H_
