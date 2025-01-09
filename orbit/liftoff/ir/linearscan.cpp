// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/linearscan.h>

using namespace liftoff::ir;

LinearScan::LinearScan(int total_regs): total_regs_(total_regs) {
    for (auto i = total_regs - 1; i >= 0; --i)
        this->free_registers_.push_back(i);
}

void LinearScan::Allocate(std::vector<LiveInterval> &intervals) {
    for (auto &interval: intervals) {
        this->ExpireOldIntervals(interval.start);

        if (this->active_.size() == this->total_regs_) {
            this->HandleSpill(&interval);
            continue;
        }

        interval.instr->assigned_reg = this->free_registers_.back();
        this->free_registers_.pop_back();

        this->active_.push_back(&interval);
    }
}

void LinearScan::ExpireOldIntervals(U32 position) {
    this->active_.erase(std::remove_if(this->active_.begin(), this->active_.end(),
                                       [&](const LiveInterval *interval) {
                                           if (interval->end <= position) {
                                               this->free_registers_.push_back(interval->instr->assigned_reg);
                                               return true;
                                           }
                                           return false;
                                       }), this->active_.end());
}

void LinearScan::HandleSpill(LiveInterval *interval) {
    auto longest = std::max_element(this->active_.begin(), this->active_.end(),
                                    [](const LiveInterval *a, const LiveInterval *b) {
                                        return a->end < b->end;
                                    });

    if ((*longest)->end > interval->end) {
        const auto spilled_instr = (*longest)->instr;

        this->spilled_.push_back(spilled_instr);

        interval->instr->assigned_reg = spilled_instr->assigned_reg;
        spilled_instr->stack_slot = 1; // TODO Impl

        this->active_.erase(longest);
        this->active_.push_back(interval);

        return;
    }

    interval->instr->assigned_reg = -1;
    this->spilled_.push_back(interval->instr);
}
