// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/ircontext.h>

using namespace liftoff::ir;

IRContext::~IRContext() noexcept {
    if (this->sub.context != nullptr) {
        const orbiter::IsolateAllocator allocator(this->isolate_);

        for (auto i = 0; i < this->sub.count; i++) {
            this->sub.context[i]->~IRContext();

            allocator.free(this->sub.context[i]);
        }

        allocator.free(this->sub.context);
    }
}

bool IRContext::ComputeLiveness() const {
    // General rule:
    // live_in = use ∪ (live_out - def)    (variables that are "live in" are those used within
    //                                     the current block or required by successor blocks,
    //                                     excluding those redefined in the current block)
    // live_out = union of live_in sets of all successor blocks

    bool changed = false;

    // Iterate through all blocks from the current one to the first, in reverse order
    for (auto *r_cursor = this->current_; r_cursor != nullptr; r_cursor = r_cursor->prev) {
        // Calculate new_live_out as union of successors' live_in
        std::unordered_set<const Symbol *> new_live_out;

        // Add to live_out all variables that are live_in in the next block
        if (r_cursor->next != nullptr)
            new_live_out.insert(r_cursor->next->live_in_.begin(), r_cursor->next->live_in_.end());

        // Add to live_out all variables that are live_in in the alternate block (alt node)
        if (r_cursor->alt != nullptr)
            new_live_out.insert(r_cursor->alt->live_in_.begin(), r_cursor->alt->live_in_.end());

        std::unordered_set<const Symbol *> new_live_in = r_cursor->use_;
        std::unordered_set<const Symbol *> diff = new_live_out;

        // Remove all variables defined in the current block (def_) from diff
        for (const auto &def: r_cursor->def_)
            diff.erase(def);

        // Add the remaining variables from diff into new_live_in
        new_live_in.insert(diff.begin(), diff.end());

        if (r_cursor->live_in_ != new_live_in || r_cursor->live_out_ != new_live_out)
            changed = true;

        // Update live_in and live_out with the newly computed values
        r_cursor->live_out_ = std::move(new_live_out);
        r_cursor->live_in_ = std::move(new_live_in);
    }

    return changed;
}

orbiter::datatype::HList IRContext::GetNamesList(U16 *known_length) const {
    MSize length = 0;

    assert(known_length != nullptr);

    *known_length = 0;

    if (this->known_props) {
        length = this->known_props->length;
        *known_length = (U16) length;
    }

    if (this->unknown_props)
        length += this->unknown_props->length;

    auto res = orbiter::datatype::ListNew(this->isolate_, length);
    if (!res)
        throw std::bad_alloc();

    ListAppend(res.get(), this->known_props.get());
    ListAppend(res.get(), this->unknown_props.get());

    return res;
}

Instruction *IRContext::GetLastActiveVariableLoad(const Symbol *symbol) {
    auto instr = this->active_regs_.find(symbol);

    if (instr != this->active_regs_.end())
        return instr->second;

    return nullptr;
}

U16 IRContext::PushKnownProps(orbiter::datatype::ORString *id) {
    if (!this->known_props) {
        this->known_props = orbiter::datatype::ListNew(this->isolate_);
        if (!this->known_props)
            throw std::bad_alloc();
    }

    const auto array = this->known_props->objects;
    for (auto i = 0; i < this->known_props->length; i++) {
        if (array[i] == (orbiter::datatype::OObject *) id)
            return (U16) i;
    }

    const auto ok = ListAppend(this->known_props.get(), (orbiter::datatype::OObject *) id);
    if (!ok)
        throw std::bad_alloc();

    return this->known_props->length - 1;
}

U16 IRContext::PushUnknownProps(orbiter::datatype::ORString *id) {
    if (!this->unknown_props) {
        this->unknown_props = orbiter::datatype::ListNew(this->isolate_);
        if (!this->unknown_props)
            throw std::bad_alloc();
    }

    const auto array = this->unknown_props->objects;
    for (auto i = 0; i < this->unknown_props->length; i++) {
        if (array[i] == (orbiter::datatype::OObject *) id)
            return (U16) i;
    }

    const auto ok = ListAppend(this->unknown_props.get(), (orbiter::datatype::OObject *) id);
    if (!ok)
        throw std::bad_alloc();

    return this->unknown_props->length - 1;
}

U16 IRContext::PushStaticValue(orbiter::datatype::OObject *value) {
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
    orbiter::IsolateAllocator allocator(this->isolate_);

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

void IRContext::AddActiveVar(const Symbol *symbol, Instruction *instr) {
    const auto defining_scope = symbol->defining_scope->type;

    if (symbol->upvalue || (defining_scope != ScopeType::FUNCTION && defining_scope != ScopeType::GENERATOR))
        return;

    this->active_regs_.insert({symbol, instr});
}

std::vector<LiveInterval> &IRContext::ComputeLiveIntervals() {
    for (auto *block = this->entry_; block != nullptr; block = block->next) {
        for (auto *instr = block->instr.head; instr != nullptr; instr = instr->next) {
            if (instr->use_list != nullptr) {
                U32 end = 0;

                for (const auto *use = instr->use_list; use != nullptr; use = use->next) {
                    if (use->user->type() != ObjectType::INSTRUCTION && use->user->type() != ObjectType::VIRT_INSTRUCTION)
                        continue;

                    const auto *u_instr = (Instruction *) use->user;
                    if (u_instr->instr_offset > end)
                        end = u_instr->instr_offset;
                }

                this->live_intervals_.emplace_back(instr, instr->instr_offset, end);
            }
        }
    }

    return this->live_intervals_;
}

void IRContext::InsertInstructionAfter(Instruction *instruction, Instruction *after) noexcept {
    after->next = instruction->next;
    after->prev = instruction;

    assert(instruction->next!=nullptr);

    instruction->next->prev = after;

    instruction->next = after;
}

void IRContext::InsertInstructionBefore(Instruction *instruction, Instruction *before) noexcept {
    before->next = instruction;
    before->prev = instruction->prev;

    assert(instruction->prev!=nullptr);

    instruction->prev->next = before;

    instruction->prev = before;
}

void IRContext::InvalidateActiveVar(const Symbol *symbol) {
    if (symbol == nullptr) {
        this->active_regs_.clear();
        return;
    }

    this->active_regs_.erase(symbol);
}

void IRContext::Delete(IRContext *context) {
    if (context == nullptr)
        return;

    const orbiter::IsolateAllocator allocator(context->isolate_);

    context->~IRContext();

    allocator.free(context);
}
