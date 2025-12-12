// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/fiber.h>

using namespace orbiter;

/**
 * @brief A thread-local pointer to the currently active Fiber object.
 *
 * This variable stores the Fiber instance that is specific to the current thread. It allows
 * the thread to access its associated Fiber object, which is used to manage execution context
 * and isolate state for fibers within the application.
 *
 * @note Each thread has its own independent instance of this variable due to its thread-local
 *       storage specifier.
 */
thread_local Fiber *thl_fiber = nullptr;

Fiber::~Fiber() {
    this->vm.stack.Cleanup(this->isolate);
    this->vm.e_stack.Cleanup(this->isolate);
}

bool Fiber::PushState() noexcept {
    constexpr auto totalSize = sizeof(FiberContext) + sizeof(Registers);

    if (!this->vm.stack.Check(this->isolate, this->vm.regs.SP.reg, totalSize))
        return false;

    auto *stack = this->vm.stack.stack + this->vm.regs.SP.reg;

    memory::MemoryCopy(stack, &this->context, sizeof(FiberContext));
    memory::MemoryCopy(stack + sizeof(FiberContext), &this->vm.regs, sizeof(Registers));

    this->vm.regs.BP.reg = this->vm.regs.SP.reg;
    this->vm.regs.SP.reg += totalSize;

    for (auto i = 0; i < kGeneralPurposeRegistersCount + 1; i++) {
        auto *value = (datatype::OObject *) *((PtrSize *) (((Register *) &this->vm.regs) + i));
        O_INCREF(value);
    }

    return true;
}

Fiber *Fiber::Current() noexcept {
    return thl_fiber;
}

Fiber *Fiber::New(Isolate *isolate, const MSize stack_size, const MSize stack_limit) noexcept {
    memory::IsolateAllocator allocator(isolate);

    const auto fiber = allocator.AllocObject<Fiber>();
    if (fiber != nullptr) {
        if (!fiber->vm.stack.Init(isolate, stack_size, stack_limit)) {
            allocator.FreeObject(fiber);

            return nullptr;
        }

        if (!fiber->vm.e_stack.Init(isolate)) {
            allocator.FreeObject(fiber);

            return nullptr;
        }

        fiber->Reset();

        fiber->isolate = isolate;
        fiber->panic_cache = nullptr;

        fiber->oom_cache = allocator.alloc<struct Panic>(sizeof(struct Panic));
        if (fiber->oom_cache == nullptr) {
            allocator.FreeObject(fiber);

            return nullptr;
        }

        fiber->oom_cache->prev = nullptr;
        fiber->oom_cache->error = nullptr;
    }

    return fiber;
}

void Fiber::SetCurrent(Fiber *fiber) noexcept {
    thl_fiber = fiber;
}

datatype::HOObject Fiber::GetDiscardPanic() noexcept {
    if (*this->panic.r_current_ == nullptr)
        return {};

    auto err = datatype::HOObject((*this->panic.r_current_)->error);

    this->DiscardPanic();

    return err;
}

void Fiber::Delete(Fiber *fiber) noexcept {
    if (fiber == nullptr)
        return;

    const memory::IsolateAllocator allocator(fiber->isolate);

    allocator.FreeObject(fiber);
}

void Fiber::DiscardPanic() noexcept {
    if (*this->panic.r_current_ == nullptr)
        return;

    auto *chain = *this->panic.r_current_;
    while (chain != nullptr) {
        orbiter::Panic *prev = nullptr;

        if (chain->error == this->isolate->oom_error_) {
            chain->error = nullptr;

            prev = chain->prev;

            chain->prev = nullptr;
            this->oom_cache = chain;

            chain = prev;

            continue;
        }

        O_FAST_DECREF(chain->error);

        chain->error = nullptr;

        prev = chain->prev;

        chain->prev = this->panic_cache;
        this->panic_cache = chain;

        chain = prev;
    }

    *this->panic.r_current_ = nullptr;
}

void Fiber::Panic(datatype::OObject *error) noexcept {
    struct Panic *p = nullptr;

    if (this->panic_cache == nullptr) {
        memory::IsolateAllocator allocator(this->isolate);

        p = allocator.alloc<struct Panic>(sizeof(struct Panic));
        if (p == nullptr)
            return;
    } else {
        p = this->panic_cache;
        this->panic_cache = this->panic_cache->prev;
    }

    p->prev = *this->panic.r_current_;
    p->error = O_FAST_INCREF(error);
    p->frame = this->vm.regs.BP.reg;

    *this->panic.r_current_ = p;
}

void Fiber::PanicOOM() noexcept {
    assert(this->oom_cache != nullptr);

    this->oom_cache->prev = *this->panic.r_current_;
    this->oom_cache->error = this->isolate->oom_error_;

    // Creates a virtually immortal object
    O_UNSAFE_GET_RC(this->isolate->oom_error_) = 0xFFFFFFA;

    *this->panic.r_current_ = this->oom_cache;
    this->oom_cache = nullptr;
}

void Fiber::PopState() noexcept {
    const auto size = this->vm.regs.SP.reg - this->vm.regs.BP.reg;

    assert(size == sizeof(FiberContext) + sizeof(Registers));

    const auto *stack_base = this->vm.stack.stack + this->vm.regs.BP.reg;

    memory::MemoryCopy(&this->context, stack_base, sizeof(FiberContext));
    memory::MemoryCopy(&this->vm.regs, stack_base + sizeof(FiberContext), sizeof(Registers));

    this->vm.regs.SP.reg = this->vm.regs.BP.reg;

    for (auto i = 0; i < kGeneralPurposeRegistersCount + 1; i++) {
        auto *value = (datatype::OObject *) *((PtrSize *) (((Register *) &this->vm.regs) + i));
        O_DECREF(value);
    }
}
