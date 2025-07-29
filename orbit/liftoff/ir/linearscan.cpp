// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0
// M.G :)

#include <orbit/liftoff/ir/linearscan.h>

using namespace liftoff::ir;

LinearScan::LinearScan(IRContext *ir, U16 total_regs) noexcept : builder_(ir),
                                                                 ir_(ir),
                                                                 total_regs_(total_regs) {
    assert(total_regs >= 2);

    for (auto i = total_regs - 1; i >= 0; --i)
        this->free_registers_.insert(static_cast<U16>(i));

    this->stack_offset_ = ir->stack_slots;
}

U16 LinearScan::GetFreeStackSlot() {
    if (!this->free_stack_slot_.empty()) {
        const auto minSlot = this->free_stack_slot_.begin();

        const auto slot = *minSlot;

        this->free_stack_slot_.erase(minSlot);

        return slot;
    }

    return this->stack_offset_++;
}

void LinearScan::AllocateSpecificRegister(LiveInterval &interval) {
    const auto find = std::find(this->free_registers_.begin(),
                                this->free_registers_.end(),
                                interval.instr->assigned_reg);

    if (find != this->free_registers_.end()) {
        this->free_registers_.erase(find);

        this->active_.insert(&interval);

        return;
    }

    auto it = active_.begin();
    while (it != active_.end()) {
        if ((*it)->instr->assigned_reg == interval.instr->assigned_reg) {
            LiveInterval *found = *it;

            if (found->end == interval.start) {
                this->active_.erase(it);

                this->active_.insert(&interval);

                return;
            }

            this->SpillToStackAndReloadUses(found->instr);

            this->active_.erase(it);

            this->active_stack_.insert(found);

            this->SpillToStackAndReloadUses(interval.instr);

            this->active_.insert(&interval);

            return;
        }

        ++it;
    }

    throw std::runtime_error("Unable to allocate specific register: logic error");
}

void LinearScan::ExpireOldIntervals(const U32 position) {
    auto it = this->active_.begin();
    while (it != this->active_.end()) {
        const LiveInterval *interval = *it;

        if (interval->end < position) {
            this->free_registers_.insert(interval->instr->assigned_reg);

            it = this->active_.erase(it);

            continue;
        }

        ++it;
    }

    it = this->active_stack_.begin();
    while (it != this->active_stack_.end()) {
        const LiveInterval *interval = *it;

        if (interval->end < position) {
            if (interval->instr->stack_slot > -1)
                this->free_stack_slot_.insert(interval->instr->stack_slot);

            it = this->active_stack_.erase(it);

            continue;
        }

        ++it;
    }
}

void LinearScan::ResolveInterferences() {
    U32 logical_counter = 0;
    U32 last_intf_point = 0;

    for (const auto *block = this->ir_->entry_; block != nullptr; block = block->next) {
        for (auto *instr = block->instr.head; instr != nullptr; instr = instr->next) {
            instr->instr_offset = logical_counter++;

            if (instr->num_ops > 0) {
                Instruction *first_intf_point = nullptr;

                for (int i = 0; i < instr->num_ops; ++i) {
                    auto *operand = (Instruction *) instr->operands[i].value;
                    if (operand != nullptr && operand->instr_offset < last_intf_point) {
                        auto *store = this->builder_.GetStackPush(operand);

                        this->ir_->InsertInstructionAfter(operand, store);

                        Instruction *load = nullptr;
                        for (const auto *user = operand->use_list; user != nullptr; user = user->next) {
                            auto *user_instr = (Instruction *) user->user;

                            if (user_instr != store
                                && user_instr->instr_offset == 0
                                || user_instr->instr_offset > last_intf_point) {
                                if (load == nullptr) {
                                    load = this->builder_.GetStackPop();

                                    this->ir_->InsertInstructionBefore(user_instr, load);
                                }

                                user_instr->ReplaceOperand(operand, load);

                                user = operand->use_list;
                            }
                        }

                        if (first_intf_point == nullptr || first_intf_point->instr_offset > operand->instr_offset)
                            first_intf_point = operand;
                    }
                }

                if (first_intf_point != nullptr) {
                    logical_counter = first_intf_point->instr_offset;
                    for (auto *cursor = first_intf_point; cursor != instr; cursor = cursor->next)
                        cursor->instr_offset = logical_counter++;
                }
            }

            if (instr->type() == ObjectType::INSTRUCTION) {
                const auto *phys = (PhysInstruction *) instr;
                if (phys->opcode == orbiter::OPCode::CALL || phys->opcode == orbiter::OPCode::EXECSUB)
                    last_intf_point = instr->instr_offset;
            }
        }
    }
}

void LinearScan::SpillAndAssignRegister(LiveInterval *interval) {
    const auto longest = *this->active_.rbegin();

    const auto spilled_instr = longest->instr;

    bool is_spilled = false;

    // Assign the register of the spilled instruction to the new interval
    interval->instr->SetRegister(spilled_instr->assigned_reg);

    // Emit instructions to store the spilled value to the stack and update references
    this->SpillToStackAndReloadUses(spilled_instr);

    // If the new interval ends after the longest, spill it to another stack slot
    if (interval->end > longest->end) {
        this->SpillToStackAndReloadUses(interval->instr);

        is_spilled = true;
    }

    this->active_stack_.insert(longest);

    this->active_.erase(longest);

    if (is_spilled)
        this->active_.insert(interval);
}

void LinearScan::SpillToStackAndReloadUses(Instruction *instruction) {
    int inserted = 0;

    // 2) Iterate through the use-list of the instruction to load the value back from the stack
    for (auto use = instruction->use_list; use != nullptr; use = use->next) {
        auto *target = (Instruction *) use->user;

        // Skip if the target is not an instruction object
        if (target->type() != ObjectType::INSTRUCTION)
            continue;

        if (target->prev == instruction)
            continue;

        // Assign a free stack slot to the spilled instruction
        if (inserted == 0)
            instruction->stack_slot = (I16) this->GetFreeStackSlot();

        // Generate a load instruction to fetch the value from the stack slot
        auto *load = this->builder_.GetLoadFromStackOffset(instruction->stack_slot, kBaseStackPointerReg);
        load->assigned_reg = instruction->assigned_reg;

        // Insert the load instruction immediately before the target instruction
        this->ir_->InsertInstructionBefore(target, load);

        // Update the operand in the target instruction to reference the load instead of the original instruction
        target->ReplaceOperand(instruction, load);

        inserted++;
    }

    if (inserted == 0)
        return;

    // 3) Generate a store instruction to save the value to the specified stack slot
    auto *store = this->builder_.GetStoreToStackOffset(instruction, kBaseStackPointerReg, instruction->stack_slot);

    // Insert the store instruction immediately after the current instruction
    this->ir_->InsertInstructionAfter(instruction, store);
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

void LinearScan::Allocate() {
    this->ResolveInterferences();

    auto live_intervals = this->ir_->ComputeLiveIntervals();
    for (auto &interval: live_intervals) {
        if (interval.instr->assigned_reg == kDoNotAllocateReg)
            continue;

        this->ExpireOldIntervals(interval.start);

        if (interval.instr->assigned_reg > kUninitializedReg) {
            this->AllocateSpecificRegister(interval);

            continue;
        }

        if (this->active_.size() == this->total_regs_) {
            this->SpillAndAssignRegister(&interval);

            continue;
        }

        auto minReg = this->free_registers_.begin();
        interval.instr->SetRegister((I16) *minReg);
        this->free_registers_.erase(minReg);

        this->active_.insert(&interval);
    }

    // Update the stack slot of the IR context to the current stack size
    if (this->stack_offset_ > this->ir_->stack_slots)
        this->builder_.AllocStackSlots(this->stack_offset_ - this->ir_->stack_slots, orbiter::AllocaFlags::DEFAULT);
}
