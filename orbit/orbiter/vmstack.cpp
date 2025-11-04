// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/vmstack.h>

using namespace orbiter;

bool VMStack::Check(Isolate *isolate, const MSize current, MSize size) noexcept {
    size = (size + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);

    if (current + size > this->capacity)
        return this->Grow(isolate, size);

    return true;
}

bool VMStack::Init(Isolate *isolate, const MSize size, const MSize stack_limit) noexcept {
    memory::IsolateAllocator allocator(isolate);

    assert(stack_limit > size);

    this->stack = allocator.alloc<Byte>(size);
    if (this->stack == nullptr)
        return false;

    this->capacity = size;
    this->limit = stack_limit;

    return true;
}

bool VMStack::Grow(Isolate *isolate, MSize size) noexcept {
    size = (size + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);

    auto increment = std::max(size, (this->capacity * kStackGrowthFactor) / kStackGrowthScalingFactor);

    if (this->capacity + increment >= this->limit) {
        increment = size;

        if (this->capacity + increment >= this->limit)
            return false;
    }

    memory::IsolateAllocator allocator(isolate);
    auto *tmp = allocator.realloc(this->stack, this->capacity + increment);
    if (tmp == nullptr)
        return false;

    this->stack = tmp;
    this->capacity += increment;

    return true;
}

void VMStack::Cleanup(Isolate *isolate) const {
    const memory::IsolateAllocator allocator(isolate);

    allocator.free(this->stack);
}
