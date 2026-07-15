// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/sync/monitor.h>

using namespace orbiter::sync;

void Monitor::SpinLock() {
    while (this->spinlock_.test_and_set(std::memory_order_acquire))
        std::this_thread::yield();
}

void Monitor::SpinUnlock() {
    this->spinlock_.clear(std::memory_order_release);
}

bool Monitor::Acquire(Fiber *fiber, const bool can_block) {
    uintptr_t expected = 0;

    if (this->lock_id_.compare_exchange_strong(expected, (uintptr_t) fiber, std::memory_order_acquire)) {
        this->locks += 1;

        return true;
    }

    if (expected == (uintptr_t) fiber) {
        this->locks += 1;

        return true;
    }

    this->SpinLock();

    expected = 0;
    if (this->lock_id_.compare_exchange_strong(expected, (uintptr_t) fiber, std::memory_order_acquire)) {
        this->locks += 1;

        this->SpinUnlock();

        return true;
    }

    if (can_block)
        this->queue_.Enqueue(fiber);

    this->SpinUnlock();

    return false;
}

void Monitor::Release() {
    this->SpinLock();

    this->locks -= 1;

    if (this->locks > 0) {
        this->SpinUnlock();

        return;
    }

    this->lock_id_.store(0, std::memory_order_release);

    auto *fiber = this->queue_.Dequeue();
    if (fiber != nullptr) {
        auto *orbiter = Orbiter::GetInstance();

        assert(orbiter != nullptr);

        // Transfers the lock to the new fiber to prevent starvation
        // in case it gets re-enqueued because someone was acquiring the monitor lock in the meantime
        this->lock_id_.store((uintptr_t) fiber, std::memory_order_release);

        orbiter->PushFiber(fiber);
    }

    this->SpinUnlock();
}
