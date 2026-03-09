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

    const memory::IsolateAllocator allocator(this->isolate);
    while (this->panic_cache != nullptr) {
        auto *prev = this->panic_cache->prev;

        allocator.free(this->panic_cache);

        this->panic_cache = prev;
    }
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

        fiber->Reset();

        fiber->isolate = isolate;
        fiber->panic_cache = nullptr;

        fiber->panic_cache = allocator.calloc<struct Panic>(sizeof(struct Panic));
        if (fiber->panic_cache == nullptr) {
            allocator.FreeObject(fiber);

            return nullptr;
        }

        fiber->panic_cache->prev = allocator.calloc<struct Panic>(sizeof(struct Panic));
        if (fiber->panic_cache->prev == nullptr) {
            allocator.FreeObject(fiber);

            return nullptr;
        }
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

    this->panic.DiscardPanic(&this->panic_cache);
}

void Fiber::Panic(datatype::OObject *error) noexcept {
    auto *p = this->panic.CreatePanic(this->isolate, &this->panic_cache, error);
    p->frame = this->vm.regs.BP.reg;
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
