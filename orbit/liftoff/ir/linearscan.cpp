// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/linearscan.h>

using namespace liftoff::ir;

LinearScan::LinearScan(IRContext *ir, U16 total_regs) noexcept : builder_(ir),
                                                                 ir_(ir),
                                                                 total_regs_(total_regs) {
    assert(total_regs >= 2);

    for (auto i = total_regs - 1; i >= 0; --i)
        this->free_registers_.push_back(i);

    this->stack_offset_ = ir->stack_slots;
}

U16 LinearScan::GetFreeStackSlot() {
    if (!this->free_stack_slot_.empty()) {
        const auto slot = this->free_stack_slot_.back();
        this->free_stack_slot_.pop_back();

        return slot;
    }

    return this->stack_offset_++;
}

void LinearScan::EmitStackLoad(Instruction *instruction) {
    // 1) Iterate through the use-list of the instruction to load the value back from the stack
    for (auto use = instruction->use_list; use != nullptr; use = use->next) {
        auto *target = (Instruction *) use->user;

        // Skip if the target is not an instruction object
        if (target->type() != ObjectType::INSTRUCTION)
            continue;

        // Generate a load instruction to fetch the value from the stack slot
        auto *load = this->builder_.GetLoadFromStackOffset(instruction->stack_slot);

        // Insert the load instruction immediately before the target instruction
        IRContext::InsertInstructionBefore(target, load);

        // Update the operand in the target instruction to reference the load instead of the original instruction
        target->ReplaceOperand(instruction, load);
    }

    // 2) Generate a store instruction to save the value to the specified stack slot
    auto *store = this->builder_.GetStoreToStackOffset(instruction, instruction->stack_slot);

    // Insert the store instruction immediately after the current instruction
    IRContext::InsertInstructionAfter(instruction, store);
}

void LinearScan::ExpireOldIntervals(U32 position) {
    auto cond_func = [&](const LiveInterval *interval) {
        // Check if the interval's end has passed the current position.
        if (interval->end < position) {
            // Reclaim the assigned register and add it back to the list of free registers.
            this->free_registers_.push_back(interval->instr->assigned_reg);

            // If the instruction has a valid stack slot, reclaim it by adding it back to the list of free stack slots.
            if (interval->instr->stack_slot > -1)
                this->free_stack_slot_.push_back(interval->instr->stack_slot);

            return true;
        }

        // If the interval has not expired, keep it in the active list.
        return false;
    };

    this->active_.erase(std::remove_if(this->active_.begin(), this->active_.end(), cond_func), this->active_.end());
}

void LinearScan::HandleSpill(LiveInterval *interval) {
    auto longest = this->active_.begin();
    for (auto iter = this->active_.begin(); iter != this->active_.end(); ++iter) {
        // Find the interval with the farthest end position in the active list
        if ((*longest)->end < (*iter)->end)
            longest = iter;
    }

    const auto spilled_instr = (*longest)->instr;

    // Assign the register of the spilled instruction to the new interval
    interval->instr->SetRegister(spilled_instr->assigned_reg);

    // Assign a free stack slot to the spilled instruction
    spilled_instr->stack_slot = (I16) this->GetFreeStackSlot();

    // Emit instructions to store the spilled value to the stack and update references
    this->EmitStackLoad(spilled_instr);

    if ((*longest)->end > interval->end) {
        // Replace the longest interval with the new interval in the active list
        this->active_.erase(longest);
        this->active_.push_back(interval);
        return;
    }

    // If the new interval ends after the longest, spill it to another stack slot
    interval->instr->stack_slot = (I16) this->GetFreeStackSlot();
    this->EmitStackLoad(interval->instr);
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

void LinearScan::Allocate() {
    for (auto &interval: this->ir_->live_intervals_) {
        if (interval.instr->assigned_reg == kDoNotAllocateReg)
            continue;

        this->ExpireOldIntervals(interval.start);

        if (this->active_.size() == this->total_regs_) {
            this->HandleSpill(&interval);
            continue;
        }

        interval.instr->SetRegister((I16) this->free_registers_.back());
        this->free_registers_.pop_back();

        this->active_.push_back(&interval);
    }

    // Update the stack slot of the IR context to the current stack size
    this->ir_->stack_slots = this->stack_offset_;
}
