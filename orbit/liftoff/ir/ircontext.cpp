// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/intervalspiller.h>

#include <orbit/liftoff/ir/ircontext.h>

using namespace liftoff::ir;

IRContext::~IRContext() noexcept {
    const orbiter::memory::IsolateAllocator allocator(this->isolate_);

    Object *next = nullptr;
    for (auto cursor = this->objs_; cursor != nullptr; cursor = next) {
        next = cursor->memory_.next;

        cursor->~Object();

        allocator.free(cursor);
    }

    if (this->sub.context != nullptr) {
        for (auto i = 0; i < this->sub.count; i++) {
            this->sub.context[i]->~IRContext();

            allocator.free(this->sub.context[i]);
        }

        allocator.free(this->sub.context);
    }
}

Instruction *IRContext::GetLastActiveVariableLoad(const Symbol *symbol) {
    const auto instr = this->active_regs_.find(symbol);

    if (instr != this->active_regs_.end())
        return instr->second;

    return nullptr;
}

const JBlock *IRContext::GetActiveContextIf(JBlockType type) const {
    const auto *cursor = this->j_chain;
    while (cursor != nullptr) {
        if (cursor->type == type)
            return cursor;

        cursor = cursor->prev;
    }

    return nullptr;
}

U16 IRContext::ExportSymbol(const Symbol *symbol, orbiter::VariableFlags flags) {
    const auto length = this->exported_names.size();

    this->exported_names.emplace_back(symbol->name, flags, symbol->offset);

    return length;
}

U16 IRContext::GetSlotFromCleanupMatch(const Instruction *start) {
    const auto it = std::find_if(cleanup_entries_.begin(), cleanup_entries_.end(),
                                 [&](const CleanupEntry &e) {
                                     return e.start == start;
                                 });

    assert(it != cleanup_entries_.end());

    return it->slot;
}

U16 IRContext::PushUnknownProps(orbiter::datatype::ORString *id) {
    if (!this->unknown_names) {
        this->unknown_names = orbiter::datatype::ListNew(this->isolate_);
        if (!this->unknown_names)
            throw std::bad_alloc();
    }

    // The ID value is the key stored in the symbol. Since symbols with identical keys
    // are always allocated at the same memory location, we can safely perform pointer
    // comparison instead of value comparison for identity checks.
    const auto array = this->unknown_names->objects;
    for (auto i = 0; i < this->unknown_names->length; i++) {
        if (array[i] == (orbiter::datatype::OObject *) id)
            return (U16) i;
    }

    const auto ok = ListAppend(this->unknown_names.get(), (orbiter::datatype::OObject *) id);
    if (!ok)
        throw std::bad_alloc();

    return this->unknown_names->length - 1;
}

U16 IRContext::PushUnknownProps(const char *id) {
    auto o_id = orbiter::datatype::ORStringNew(this->isolate_, id);
    if (!o_id)
        throw std::bad_alloc();

    return this->PushUnknownProps(o_id.get());
}

U16 IRContext::PushStaticValue(orbiter::datatype::OObject *value) {
    // TODO: check if an element equal to the one you are loading already exists!
    if (!this->static_values) {
        this->static_values = orbiter::datatype::ListNew(this->isolate_);
        if (!this->static_values)
            throw std::bad_alloc();
    }

    if (!ListAppend(this->static_values.get(), value))
        throw std::bad_alloc();

    return this->static_values->length - 1;
}

U16 IRContext::PushSubContext(IRContext *context) {
    orbiter::memory::IsolateAllocator allocator(this->isolate_);

    if (this->sub.context == nullptr) {
        this->sub.context = allocator.alloc<IRContext *>(8 * sizeof(void *));
        if (this->sub.context == nullptr)
            throw std::bad_alloc();

        this->sub.count = 0;
        this->sub.size = 8;
    }

    if (this->sub.count + 1 >= this->sub.size) {
        const auto tmp = allocator.realloc<IRContext *>(this->sub.context,
                                                        (this->sub.size + 8) * sizeof(void *));
        if (tmp == nullptr)
            throw std::bad_alloc();

        this->sub.context = tmp;
        this->sub.size += 8;
    }

    context->back = this;
    this->sub.context[this->sub.count] = context;

    return this->sub.count++;
}

std::vector<LiveInterval> &IRContext::ComputeLiveIntervals() {
    this->live_intervals_.clear();

    this->SlotIndexes();

    for (const auto *block = this->entry_; block != nullptr; block = block->next) {
        for (auto *instr = block->instr.head; instr != nullptr; instr = instr->next) {
            if (instr->use_list != nullptr) {
                U32 end = 0;

                for (const auto *use = instr->use_list; use != nullptr; use = use->next) {
                    if (use->user->type() != ObjectType::INSTRUCTION
                        && use->user->type() != ObjectType::VIRT_INSTRUCTION)
                        continue;

                    const auto *i_user = (Instruction *) use->user;
                    if (i_user->instr_offset > end)
                        end = i_user->instr_offset;
                }

                this->live_intervals_.emplace_back(instr, instr->instr_offset, end);

                continue;
            }

            // No users. DCE has already dropped the pure value-producers, so
            // whatever is left here has side effects. If it still DEFINES a
            // register it must be allocated one anyway — a discarded `await` /
            // `<- ch`, or a Phi join whose value is unused (e.g. `a || b` as a
            // statement). Otherwise codegen emits kUninitializedReg (-1) into
            // the 4-bit DST field, corrupting it.
            // A point interval suffices: it expires immediately after the def.
            //
            // A Phi (VIRT_INSTRUCTION) always defines a register: it inherits
            // one and propagates it to its kDoNotAllocateReg targets.
            const bool defines = instr->type() == ObjectType::VIRT_INSTRUCTION
                                 || (
                                     instr->type() == ObjectType::INSTRUCTION
                                     && OpcodeDefinesRegister(((PhysInstruction *) instr)->opcode)
                                 );

            if (defines)
                this->live_intervals_.emplace_back(instr, instr->instr_offset, instr->instr_offset);
        }
    }

    return this->live_intervals_;
}

void IRContext::AddActiveVar(const Symbol *symbol, Instruction *instr) {
    if (symbol->location == StorageLocation::CLOSURE)
        return;

    this->active_regs_.insert({symbol, instr});
}

void IRContext::CallerSaveSpiller() {
    std::vector<U32> callers;

    if (live_intervals_.empty())
        return;

    // Pre-scan: collect the instruction offset of every call site, in program
    // order. CALL/EXECSUB/NTCALL are caller-clobbered in Orbit's VM — the callee
    // may overwrite every general-purpose register — so any value that is live
    // across one of these sites must be saved to the stack beforehand and
    // reloaded at each use that follows the call. The offsets come out ascending
    // because we walk blocks and instructions in layout order, which lets the
    // spill loop below stop early once a call is past an interval's end.
    for (const auto *block = this->entry_; block != nullptr; block = block->next) {
        for (auto instr = block->instr.head; instr != nullptr; instr = instr->next) {
            if (instr->type() == ObjectType::INSTRUCTION) {
                const auto opcode = ((PhysInstruction *) instr)->opcode;
                if (opcode == orbiter::OPCode::CALL
                    || opcode == orbiter::OPCode::EXECSUB
                    || opcode == orbiter::OPCode::NTCALL) {
                    callers.push_back(instr->instr_offset);
                }
            }
        }
    }

    IntervalSpiller spiller(this);

    for (auto &interval: this->live_intervals_) {
        for (const auto caller: callers) {
            if (caller > interval.end)
                break;

            if (caller > interval.start && caller <= interval.end) {
                spiller.Spill(&interval, caller);

                break;
            }
        }
    }
}

void IRContext::Delete(IRContext *context) {
    if (context == nullptr)
        return;

    assert(context->back == nullptr);

    const orbiter::memory::IsolateAllocator allocator(context->isolate_);

    context->~IRContext();

    allocator.free(context);
}

void IRContext::DeleteInstruction(Instruction *instruction) noexcept {
    if (instruction == nullptr)
        return;

    this->current_->DeleteInstruction(instruction);

    this->RemoveFromObjList(instruction);

    instruction->~Instruction();

    const orbiter::memory::IsolateAllocator allocator(this->isolate_);

    allocator.free(instruction);
}

void IRContext::InvalidateActiveVar(const Symbol *symbol) {
    if (symbol == nullptr) {
        this->active_regs_.clear();
        return;
    }

    this->active_regs_.erase(symbol);
}

void IRContext::InsertInstructionAfter(Instruction *instruction, Instruction *after) noexcept {
    instruction->basic_block->AddInstructionAfter(instruction, after);
}

void IRContext::InsertInstructionBefore(Instruction *instruction, Instruction *before) noexcept {
    instruction->basic_block->AddInstructionBefore(instruction, before);
}

void IRContext::RemoveFromObjList(Object *obj) noexcept {
    auto *next = obj->memory_.next;
    auto *prev = obj->memory_.prev;

    obj->memory_.next = nullptr;
    obj->memory_.prev = nullptr;

    if (next != nullptr)
        next->memory_.prev = prev;

    if (prev == nullptr) {
        this->objs_ = next;

        return;
    }

    prev->memory_.next = next;
}

void IRContext::SlotIndexes() const noexcept {
    U32 index = 0;

    for (const auto *b_cursor = this->entry_; b_cursor != nullptr; b_cursor = b_cursor->next) {
        for (auto *instr = b_cursor->instr.head; instr != nullptr; instr = instr->next)
            instr->instr_offset = index++;
    }
}
