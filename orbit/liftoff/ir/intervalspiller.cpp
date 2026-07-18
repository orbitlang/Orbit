// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/intervalspiller.h>

using namespace liftoff::ir;

U16 IntervalSpiller::GetFreeStackSlot() {
    if (!this->free_stack_slot_.empty()) {
        const auto minSlot = this->free_stack_slot_.begin();

        const auto slot = *minSlot;

        this->free_stack_slot_.erase(minSlot);

        return slot;
    }

    return this->stack_offset_++;
}

void IntervalSpiller::ExpireOldIntervals(const LiveInterval *interval) {
    auto it = this->active_stack_.begin();
    while (it != this->active_stack_.end()) {
        const LiveInterval *stored = *it;

        if (stored->end < interval->start) {
            this->free_stack_slot_.insert(stored->instr->stack_slot);

            it = this->active_stack_.erase(it);
        } else
            ++it;
    }
}

void IntervalSpiller::Commit() {
    // Update the stack slot of the IR context to the current stack size
    if (this->stack_offset_ > this->builder_.context->stack_slots_max)
        this->builder_.AllocStackSlots(this->stack_offset_ - this->builder_.context->stack_slots_max,
                                       orbiter::AllocaFlags::DEFAULT);
}

void IntervalSpiller::Spill(const LiveInterval *interval, const U32 after) {
    this->ExpireOldIntervals(interval);

    auto *instruction = interval->instr;

    const auto had_slot = instruction->stack_slot >= 0;

    auto ld_opcode = orbiter::OPCode::SKLDR;
    auto ld_offset = (I16) 0;
    auto inserted = 0;

    auto use = instruction->use_list;
    if (use == nullptr)
        return;

    auto *target = (PhysInstruction *) use->user;

    // Store-to-load forwarding: if the immediately following instruction is a
    // STGOFF, the value will already be persisted in a global slot — reuse it
    // instead of allocating a separate stack spill slot.
    if (target->type() == ObjectType::INSTRUCTION
        && target->opcode == orbiter::OPCode::STGOFF
        && target->prev == instruction) {
        ld_opcode = orbiter::OPCode::LDGOFF;
        ld_offset = ((OffsetInstruction *) target)->offset;
    }

    while (use != nullptr) {
        target = (PhysInstruction *) use->user;

        if (target->type() != ObjectType::INSTRUCTION) {
            use = use->next;

            continue;
        }

        if (target->instr_offset <= (after)) {
            use = use->next;

            continue;
        }

        Instruction *load = nullptr;
        if (ld_opcode == orbiter::OPCode::SKLDR) {
            // Allocate a stack slot on the first reload — reused for all subsequent ones.
            if (inserted == 0 && !had_slot)
                instruction->stack_slot = (I16) this->GetFreeStackSlot();

            load = this->builder_.GetLoadFromStackOffset(kBaseStackPointerReg, instruction->stack_slot);
        } else {
            // The value lives in a global slot — emit a direct LDGOFF.
            load = this->builder_.GetLoadFromGlobalOffset(ld_offset);
        }

        if (this->inherit_reg_)
            load->assigned_reg = instruction->assigned_reg;

        IRContext::InsertInstructionBefore(target, load);

        // Replace the operand reference so the target now consumes the reloaded value.
        // ReplaceOperand calls DeleteUse, which unlinks `use` from the list — the
        // only safe way to keep iterating is to restart from the head of the (now
        // shorter) use_list.
        target->ReplaceOperand(instruction, load);

        inserted += 1;

        use = instruction->use_list;
    }

    // No SKSTR needed if:
    //   - no non-adjacent uses were found (nothing to reload)
    //   - a slot was already present from a previous spill (SKSTR was already emitted)
    //   - the backing location is a global slot (STGOFF already covers the save)
    if (inserted == 0 || had_slot || ld_opcode != orbiter::OPCode::SKLDR)
        return;

    auto *store = this->builder_.GetStoreToStackOffset(instruction, kBaseStackPointerReg, instruction->stack_slot);

    IRContext::InsertInstructionAfter(instruction, store);

    this->active_stack_.push_back(interval);
}
