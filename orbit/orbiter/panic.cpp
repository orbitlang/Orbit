// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/memory/iallocator.h>

#include <orbit/orbiter/isolate.h>
#include <orbit/orbiter/panic.h>

using namespace orbiter;

Panic *PanicContainer::CreatePanic(Isolate *isolate, Panic **panic_cache, datatype::OObject *error) const noexcept {
    Panic *panic = nullptr;

    assert(*panic_cache != nullptr);

    if ((*panic_cache)->prev == nullptr) {
        memory::IsolateAllocator allocator(isolate);

        panic = allocator.alloc<Panic>(sizeof(Panic));
        if (panic == nullptr)
            return this->CreateOOMPanic(isolate, panic_cache);
    } else {
        panic = *panic_cache;
        *panic_cache = panic->prev;
    }

    panic->prev = *this->r_current_;
    panic->error = O_FAST_INCREF(error);
    panic->frame = 0; // Managed externally by Fiber::Panic or other components as needed

    *this->r_current_ = panic;

    return panic;
}

Panic *PanicContainer::CreateOOMPanic(const Isolate *isolate, Panic **panic_cache) const {
    assert(*panic_cache != nullptr);

    auto *panic = *panic_cache;
    *panic_cache = panic->prev;

    panic->prev = *this->r_current_;
    panic->error = isolate->oom_error_;

    // Creates a virtually immortal object
    O_UNSAFE_GET_RC(isolate->oom_error_) = 0xFFFFFFA;

    *this->r_current_ = panic;

    return panic;
}

void PanicContainer::DiscardPanic(Panic **panic_cache) const noexcept {
    if (*this->r_current_ == nullptr)
        return;

    auto *chain = *this->r_current_;
    while (chain != nullptr) {
        O_FAST_DECREF(chain->error);

        chain->error = nullptr;

        auto *prev = chain->prev;

        chain->prev = *panic_cache;
        *panic_cache = chain;

        chain = prev;
    }

    *this->r_current_ = nullptr;
}

void PanicContainer::Reset() noexcept {
    this->current_ = nullptr;
    this->r_current_ = &this->current_;
}
