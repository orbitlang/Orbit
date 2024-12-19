// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/ircontext.h>

using namespace liftoff::ir;

IRContext::~IRContext() noexcept {
    if (this->sub.context != nullptr) {
        const orbiter::IsolateAllocator allocator(this->isolate_);

        for (auto i = 0; i < this->sub.count; i++)
            Delete(this->sub.context[i]);

        allocator.free(this->sub.context);
    }
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

Instruction *IRContext::GetLastActiveVariableLoad(const Symbol *symbol) {
    auto instr = this->active_regs_.find(symbol);

    if(instr != this->active_regs_.end())
        return instr->second;

    return nullptr;
}

void IRContext::InvalidateActiveVar(const Symbol *symbol) {
    if(symbol == nullptr) {
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
