// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/memory/iallocator.h>

using namespace orbiter::memory;

IsolateAllocator::IsolateAllocator(Isolate *isolate) noexcept : isolate_(isolate), allocator_(isolate->allocator_) {
}

void *IsolateAllocator::Alloc(const size_type size) const noexcept {
    auto *data = this->allocator_->Alloc(size);
    if (data == nullptr)
        Orbiter::RuntimeOOMPanic(this->isolate_);

    return data;
}

void *IsolateAllocator::Realloc(void *ptr, const size_type size) const noexcept {
    auto *data = this->allocator_->Realloc(ptr, size);
    if (data == nullptr)
        Orbiter::RuntimeOOMPanic(this->isolate_);

    return data;
}
