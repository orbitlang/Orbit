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
        if (value != nullptr && O_IS_OBJECT(value))
            O_INCREF(value);
    }

    return true;
}

Fiber *Fiber::Current() noexcept {
    return thl_fiber;
}

void Fiber::SetCurrent(Fiber *fiber) noexcept {
    thl_fiber = fiber;
}

Fiber *Fiber::New(Isolate *isolate, MSize stack_size, MSize stack_limit) noexcept {
    memory::IsolateAllocator allocator(isolate);

    const auto fiber = allocator.alloc<Fiber>(sizeof(Fiber));
    if (fiber != nullptr) {
        if (!fiber->vm.stack.Init(isolate, stack_size, stack_limit)) {
            allocator.free(fiber);
            return nullptr;
        }

        fiber->Reset();

        fiber->isolate = isolate;
    }

    return fiber;
}

void Fiber::Delete(Fiber *fiber) noexcept {
    if (fiber == nullptr)
        return;

    // todo
    assert(false);
}

void Fiber::PopState() noexcept {
    auto size = this->vm.regs.SP.reg - this->vm.regs.BP.reg;

    assert(size == sizeof(FiberContext) + sizeof(Registers));

    auto *stack_base = this->vm.stack.stack + this->vm.regs.BP.reg;

    memory::MemoryCopy(&this->context, stack_base, sizeof(FiberContext));
    memory::MemoryCopy(&this->vm.regs, stack_base + sizeof(FiberContext),sizeof(Registers));

    for (auto i = 0; i < kGeneralPurposeRegistersCount + 1; i++) {
        auto *value = (datatype::OObject *) *((PtrSize *) (((Register *) &this->vm.regs) + i));
        if (value != nullptr && O_IS_OBJECT(value))
            Release(value);
    }
}
