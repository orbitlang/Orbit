// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/memory/iallocator.h>

#include <orbit/orbiter/datatype/error.h>

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/panic.h>

using namespace orbiter;

int orbiter::PanicFormat(const PanicContainer *container, char *out, const size_t out_size) noexcept {
    if (out == nullptr || out_size == 0)
        return 0;

    if (container == nullptr || container->current_ == nullptr) {
        out[0] = '\0';

        return 0;
    }

    auto remaining = out_size;
    auto total = 0;
    auto depth = 0;

    auto *cursor = out;

    // Walk newest → oldest.  The first entry is the panic that is
    // currently being unwound; subsequent entries are older panics that
    // were already in flight and got "shadowed" when the newer ones were
    // raised (typically during cleanup / defer handlers).
    auto *p = container->current_;
    while (p != nullptr) {
        if (depth > 0) {
            const auto n = std::snprintf(cursor, remaining, "\n  while handling: ");
            if (n < 0)
                return total;

            total += n;

            if (n >= remaining)
                return total;

            cursor += n;

            remaining -= n;
        }

        const int n = datatype::ErrorFormat((const datatype::Error *) p->error, cursor, remaining);
        if (n < 0)
            return total;

        total += n;

        if (n >= remaining)
            return total;

        cursor += n;

        remaining -=  n;

        p = p->prev;

        depth++;
    }

    return total;
}

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
    panic->error = error;
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

    *this->r_current_ = panic;

    return panic;
}

void PanicContainer::DiscardPanic(Panic **panic_cache) const noexcept {
    if (*this->r_current_ == nullptr)
        return;

    auto *chain = *this->r_current_;
    while (chain != nullptr) {
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
