// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_LINEARSCAN_H_
#define ORBIT_LIFTOFF_IR_LINEARSCAN_H_

#include <set>

#include <orbit/liftoff/ir/builder.h>
#include <orbit/liftoff/ir/intervalspiller.h>
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

        Builder builder_;

        IntervalSpiller spiller_;

        std::set<LiveInterval *, IntervalEndComparator> active_;

        std::set<U16> free_registers_;

        const U16 total_regs_;

        void AllocateSpecificRegister(LiveInterval &interval);

        void ExpireOldIntervals(U32 position);

        void SpillAndAssignRegister(LiveInterval *interval);

    public:
        explicit LinearScan(IRContext *ir, U16 total_regs) noexcept;

        void Allocate(std::vector<LiveInterval> &intervals);
    };
}

#endif // !ORBIT_LIFTOFF_IR_LINEARSCAN_H_
