// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/excstack.h>

using namespace orbiter;

bool VMExcStack::Init(Isolate *isolate, const U16 exceptions) noexcept {
    memory::IsolateAllocator allocator(isolate);

    assert(exceptions >= kExceptionStackMinSize);

    this->stack = allocator.alloc<ExceptionContext>(kExceptionStackSize);
    if (this->stack == nullptr)
        return false;

    this->count = 0;
    this->limit = exceptions;

    return true;
}

ExceptionContext *VMExcStack::Push(ExceptionContext *prev, const U32 joffset) noexcept {
    ExceptionContext *context = nullptr;

    if (this->count + 1 > this->limit)
        return nullptr;

    context = this->stack + this->count;

    context->prev = prev;
    context->joffset = joffset;

    context->ret_value = 0;

    this->count += 1;

    return context;
}

void VMExcStack::Cleanup(Isolate *isolate) const {
    const memory::IsolateAllocator allocator(isolate);

    allocator.free(this->stack);
}

void VMExcStack::Pop() noexcept {
    if (this->count > 0) {
        this->count -= 1;

        return;
    }

    assert(false);
}
