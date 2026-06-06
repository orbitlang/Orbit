// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0
// M.G :)

#include <orbit/liftoff/ir/linearscan.h>

using namespace liftoff::ir;

LinearScan::LinearScan(IRContext *ir, const U16 total_regs) noexcept : builder_(ir),
                                                                 ir_(ir),
                                                                 total_regs_(total_regs) {
    assert(total_regs >= 2);

    for (auto i = total_regs - 1; i >= 0; --i)
        this->free_registers_.insert(static_cast<U16>(i));

    this->stack_offset_ = ir->stack_slots_max;
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

std::vector<U32> LinearScan::CallPositionPreScan() const {
    // Pre-scan: collect the instruction offsets of every CALL and EXECSUB in
    // program order.  CALL/EXECSUB are caller-clobbered in Orbit's VM — the
    // callee may overwrite every general-purpose register — so any value that
    // is live across one of these sites must be saved to the stack beforehand
    // and reloaded at each use that follows the call.
    //
    // We cannot detect calls through the live-interval list alone: a CALL with
    // no users never appears as an interval.  A dedicated IR walk here is the
    // only reliable way to find all call sites.
    std::vector<U32> call_positions;

    for (const auto *block = this->ir_->entry_; block != nullptr; block = block->next) {
        for (auto *instr = block->instr.head; instr != nullptr; instr = instr->next) {
            if (instr->type() != ObjectType::INSTRUCTION) continue;

            const auto op = ((PhysInstruction *) instr)->opcode;

            if (op == orbiter::OPCode::CALL || op == orbiter::OPCode::EXECSUB)
                call_positions.push_back(instr->instr_offset);
        }
    }

    return std::move(call_positions);
}

// Allocates a register that is already pinned by the instruction (e.g. CALL always
// returns into R13, ITRNXT into R13, etc.). If the register is free, it is simply
// claimed. If it is occupied by another interval, one or both intervals must be spilled.
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

            // If the conflicting interval ends exactly where the new one starts,
            // they do not actually overlap — hand the register over directly.
            if (found->end == interval.start) {
                this->active_.erase(it);

                this->active_.insert(&interval);

                return;
            }

            // True overlap: evict the conflicting interval to the stack, then
            // also spill the incoming interval if it outlives the evicted one.
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

// Called when all registers are occupied and a new interval still needs one.
// Evicts the interval with the longest remaining lifetime (it is cheapest to
// keep short-lived values in registers), steals its register for the incoming
// interval, and emits SKSTR/SKLDR pairs to preserve the evicted value.
void LinearScan::SpillAndAssignRegister(LiveInterval *interval) {
    const auto longest = *this->active_.rbegin();

    const auto spilled_instr = longest->instr;

    bool is_spilled = false;

    // Assign the register of the spilled instruction to the new interval
    interval->instr->SetRegister(spilled_instr->assigned_reg);

    // Emit instructions to store the spilled value to the stack and update references
    this->SpillToStackAndReloadUses(spilled_instr);

    // If the incoming interval outlives the evicted one, the incoming interval will
    // itself need to be on the stack for part of its range — spill it too.
    if (interval->end > longest->end) {
        this->SpillToStackAndReloadUses(interval->instr);

        is_spilled = true;
    }

    this->active_stack_.insert(longest);

    this->active_.erase(longest);

    if (is_spilled)
        this->active_.insert(interval);
}

// Spills `instruction` to a backing location and replaces all non-adjacent uses
// with a reload. Called both for register-pressure evictions and cross-call saves.
//
// Backing location strategy (store-to-load forwarding):
//   If the instruction is immediately followed by a STGOFF that stores its result
//   to a known global offset, that global slot already holds the value — no extra
//   SKSTR is needed. Post-spill reloads are emitted as LDGOFF from the same offset.
//   Otherwise the value is saved to a fresh stack slot via SKSTR/SKLDR.
//
// If the instruction already has a stack_slot (had_slot == true), it was spilled
// earlier — the existing slot is reused and no new SKSTR is emitted.
void LinearScan::SpillToStackAndReloadUses(Instruction *instruction) {
    const bool had_slot = instruction->stack_slot >= 0;

    int inserted = 0;

    // Default reload strategy: stack slot via SKLDR.
    // Switched to LDGOFF if a companion STGOFF is detected below.
    auto ld_opcode = orbiter::OPCode::SKLDR;
    auto ld_offset = (I16) 0;

    for (auto use = instruction->use_list; use != nullptr; use = use->next) {
        auto *target = (PhysInstruction *) use->user;

        if (target->type() != ObjectType::INSTRUCTION)
            continue;

        // Store-to-load forwarding: if the immediately following instruction is a
        // STGOFF, the value will already be persisted in a global slot — reuse it
        // instead of allocating a separate stack spill slot.
        if (target->opcode == orbiter::OPCode::STGOFF
            && target->instr_offset - 1 == instruction->instr_offset) {
            ld_opcode = orbiter::OPCode::LDGOFF;
            ld_offset = ((OffsetInstruction *) target)->offset;
        }

        // Skip uses that are immediately adjacent to the producing instruction —
        // the value is still live in the register at that point, no reload needed.
        //
        // The check is split in two because LinearScan inserts new instructions
        // (SKSTR, SKLDR) into the IR while allocation is in progress. After an
        // insertion between `instruction` and `target`, target->prev is no longer
        // `instruction` even though the two are logically adjacent.
        //
        //   target->prev == instruction
        //     Fast path: no insertions have happened between the two yet.
        //
        //   target->instr_offset - 1 == instruction->instr_offset
        //     Fallback: something was inserted in between, but the original offsets
        //     (assigned by SlotIndexes before LinearScan runs) still reflect the
        //     original adjacency. This works because newly inserted instructions
        //     always carry instr_offset = 0 and never shift the offsets of the
        //     surrounding original instructions.
        if (target->prev == instruction || target->instr_offset - 1 == instruction->instr_offset)
            continue;

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

        load->assigned_reg = instruction->assigned_reg;

        IRContext::InsertInstructionBefore(target, load);

        // Replace the operand reference so the target now consumes the reloaded value.
        // ReplaceOperand calls DeleteUse, which sets use->next = nullptr — iterating
        // further via use->next would stop the loop prematurely. Restarting from the
        // head of the (now shorter) use_list is the only safe approach.
        target->ReplaceOperand(instruction, load);
        use = instruction->use_list;

        inserted++;

        if (use == nullptr)
            break;
    }

    // No SKSTR needed if:
    //   - no non-adjacent uses were found (nothing to reload)
    //   - a slot was already present from a previous spill (SKSTR was already emitted)
    //   - the backing location is a global slot (STGOFF already covers the save)
    if (inserted == 0 || had_slot || ld_opcode != orbiter::OPCode::SKLDR)
        return;

    auto *store = this->builder_.GetStoreToStackOffset(instruction, kBaseStackPointerReg, instruction->stack_slot);

    IRContext::InsertInstructionAfter(instruction, store);
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

void LinearScan::Allocate(std::vector<LiveInterval> &intervals) {
    auto call_positions = this->CallPositionPreScan();
    auto call_it = call_positions.begin();

    for (auto &interval: intervals) {
        if (interval.instr->assigned_reg == kDoNotAllocateReg)
            continue;

        // Process cross-call spills BEFORE expiring intervals.
        //
        // An interval can be live across a call (end > call_pos) yet still
        // end before the start of the next interval we process here. If we
        // expire first, such an interval is removed from active_ and the
        // spill loop below never sees it — so the register clobbered by the
        // call is never reloaded.
        //
        // Spilling first catches every interval that was alive at the call
        // even if it has already ended by the time we get here; the
        // subsequent ExpireOldIntervals then cleans up correctly.
        //
        // For every call site that falls at or before the start of this interval,
        // spill all register-resident values that outlive the call.
        // SpillToStackAndReloadUses emits a SKSTR right after the producing
        // instruction and inserts a SKLDR (or LDGOFF) before each use that
        // comes after the call, transparently restoring the value into its
        // original register at each consumption point.
        // The interval stays in active_ because it keeps its register — only
        // the backing store and the reload stubs are added.
        while (call_it != call_positions.end() && *call_it <= interval.start) {
            for (const auto *active: this->active_) {
                if (active->end > *call_it)
                    this->SpillToStackAndReloadUses(active->instr);
            }

            ++call_it;
        }

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
    if (this->stack_offset_ > this->ir_->stack_slots_max)
        this->builder_.AllocStackSlots(this->stack_offset_ - this->ir_->stack_slots, orbiter::AllocaFlags::DEFAULT);
}
