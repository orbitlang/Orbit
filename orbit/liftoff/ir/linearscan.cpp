// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0
// M.G :)

#include <orbit/liftoff/ir/linearscan.h>

using namespace liftoff::ir;

LinearScan::LinearScan(IRContext *ir, const U16 total_regs) noexcept : builder_(ir), spiller_(ir, true),
                                                                       total_regs_(total_regs) {
    assert(total_regs >= 2);

    for (auto i = total_regs - 1; i >= 0; --i)
        this->free_registers_.insert(static_cast<U16>(i));
}

void LinearScan::AllocateSpecificRegister(LiveInterval &interval) {
    const auto reg = interval.instr->assigned_reg;

    // Conflict scan first: the free pool alone cannot tell whether the register
    // is busy, because pinned registers (e.g.: RR) live outside it.
    // Only active_ knows who currently holds them.
    auto it = this->active_.begin();
    while (it != this->active_.end()) {
        if ((*it)->instr->assigned_reg == reg) {
            const LiveInterval *found = *it;

            // If the conflicting interval ends exactly where the new one starts,
            // they do not actually overlap, hand the register over directly.
            if (found->end == interval.start) {
                this->active_.erase(it);

                this->active_.insert(&interval);

                return;
            }

            // True overlap: from interval.start onward the register is shared scratch
            this->spiller_.Spill(found, interval.start);
            this->spiller_.Spill(&interval, interval.start);

            // Only the longer-lived of the two stays in active_ as the
            // register's owner-of-record.
            if (interval.end > found->end) {
                this->active_.erase(it);

                this->active_.insert(&interval);
            }

            return;
        }

        ++it;
    }

    // No conflict. A pool register must be sitting in the free pool; a pinned
    // register outside the pool is simply claimed, active_ tracks it so
    // later pinned intervals conflict against it.
    if (reg < this->total_regs_) {
        const auto find = std::find(this->free_registers_.begin(),
                                    this->free_registers_.end(),
                                    reg);

        if (find == this->free_registers_.end())
            throw std::runtime_error("Unable to allocate specific register: logic error");

        this->free_registers_.erase(find);
    }

    this->active_.insert(&interval);
}

void LinearScan::ExpireOldIntervals(const U32 position) {
    auto it = this->active_.begin();

    while (it != this->active_.end()) {
        const auto *interval = *it;

        // Strict `<`: an interval is expired only once `position` has passed its
        // end. NOTE the deliberate inconsistency with AllocateSpecificRegister,
        // which treats `found->end == interval.start` as non-overlapping and
        // hands the register over. Relaxing this to `<=` (freeing a register at
        // end == start) was tried and empirically miscompiles under register
        // pressure, the read-before-write argument does not hold in every path,
        // so the conservative bound stays here. See the boundary discussion.
        if (interval->end < position) {
            // Only pool registers go back to the free pool.
            if (interval->instr->assigned_reg < this->total_regs_)
                this->free_registers_.insert(interval->instr->assigned_reg);

            it = this->active_.erase(it);

            continue;
        }

        ++it;
    }
}

void LinearScan::SpillAndAssignRegister(LiveInterval *interval) {
    // Pick the longest-lived interval that holds a POOL register: active_ also
    // tracks pinned intervals (RR call results), whose register must never be
    // handed to an ordinary value.
    LiveInterval *longest = nullptr;
    for (auto rit = this->active_.rbegin(); rit != this->active_.rend(); ++rit) {
        if ((*rit)->instr->assigned_reg < this->total_regs_) {
            longest = *rit;

            break;
        }
    }

    // The free pool is empty, so every pool register is held by an active interval.
    assert(longest != nullptr);

    // The incoming interval always takes the longest-lived interval's register.
    interval->instr->SetRegister(longest->instr->assigned_reg);

    // From interval->start onward the register is shared scratch.
    this->spiller_.Spill(longest, interval->start);
    this->spiller_.Spill(interval, interval->start);

    // Only the longer-lived of the two stays in active_ as the register's owner-of-record.
    if (interval->end > longest->end) {
        this->active_.erase(longest);

        this->active_.insert(interval);
    }
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

void LinearScan::Allocate(std::vector<LiveInterval> &intervals) {
    for (auto &interval: intervals) {
        if (interval.instr->assigned_reg == kDoNotAllocateReg)
            continue;

        this->ExpireOldIntervals(interval.start);

        if (interval.instr->assigned_reg > kUninitializedReg) {
            this->AllocateSpecificRegister(interval);

            continue;
        }

        if (this->free_registers_.empty()) {
            this->SpillAndAssignRegister(&interval);

            continue;
        }

        auto minReg = this->free_registers_.begin();

        interval.instr->SetRegister((I16) *minReg);

        this->free_registers_.erase(minReg);

        this->active_.insert(&interval);
    }

    // Update the stack slot of the IR context to the current stack size
    this->spiller_.Commit();
}
