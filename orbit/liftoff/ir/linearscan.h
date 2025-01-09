// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_LINEARSCAN_H_
#define ORBIT_LIFTOFF_IR_LINEARSCAN_H_

#include <vector>

#include <orbit/liftoff/ir/ircontext.h>

namespace liftoff::ir {
    class LinearScan {
        const int total_regs_;

        std::vector<LiveInterval *> active_;
        std::vector<I16> free_registers_;
        std::vector<Instruction *> spilled_;

        void ExpireOldIntervals(U32 position);

        void HandleSpill(LiveInterval *interval);

    public:
        explicit LinearScan(int total_regs);

        void Allocate(std::vector<LiveInterval> &intervals);
    };
}

#endif // !ORBIT_LIFTOFF_IR_LINEARSCAN_H_
