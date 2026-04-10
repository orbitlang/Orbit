// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/util/macros.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

#if defined(_ORBIT_PLATFORM_WINDOWS)
#include <Windows.h>

#define OS_WAIT(ptr, value)                                         \
    do {                                                            \
        auto stored = value;                                        \
        WaitOnAddress(ptr, &stored, sizeof(MutexWord), INFINITE);   \
    } while(0)

#define OS_WAKE(ptr)            WakeByAddressSingle(ptr)

#elif defined(_ORBIT_PLATFORM_DARWIN)
extern "C" int __ulock_wait(unsigned int operation, void *addr, unsigned long long value, unsigned int timeout);
extern "C" int __ulock_wake(unsigned int operation, void *addr, unsigned long long wake_value);

#define UL_COMPARE_AND_WAIT     1

#define OS_WAIT(ptr, value)     __ulock_wait(UL_COMPARE_AND_WAIT, ptr, value, 0)
#define OS_WAKE(ptr)            __ulock_wake(UL_COMPARE_AND_WAIT, ptr, 0)

#elif defined(_ORBIT_PLATFORM_LINUX)
#include <linux/futex.h>
#include <syscall.h>
#include <unistd.h>

#define futex_wait(ptr, value)  syscall(SYS_futex, ptr, FUTEX_WAIT, value, nullptr, nullptr, 0)
#define futex_wake(ptr)         syscall(SYS_futex, ptr, FUTEX_WAKE, 1, nullptr, nullptr, 0)

#define OS_WAIT(ptr, value)     futex_wait(ptr, value)
#define OS_WAKE(ptr)            futex_wake(ptr)

#else
#error "unsupported platform"
#endif

using namespace orbiter::sync;

// PRIVATE

void AsyncRWLock::lock_shared_slow(std::thread::id id) {
    auto current = this->lock_.load(std::memory_order_acquire);
    MutexBits desired{};

    do {
        while (current.is_ulocked() && this->id_ != id) {
            this->pending_++;
            OS_WAIT(&this->lock_, current.value());
            this->pending_--;

            current = this->lock_.load(std::memory_order_acquire);
        }

        desired = current;
        desired.inc_shared();
    } while (!this->lock_.compare_exchange_strong(current, desired));
}

void AsyncRWLock::lock_slow() {
    auto current = MutexBits{};
    auto desired = MutexBits{};

    desired.acquire_unique();

    while (!this->lock_.compare_exchange_strong(current, desired)) {
        this->pending_++;
        OS_WAIT(&this->lock_, current.value());
        this->pending_--;

        current = MutexBits{};
    }
}

// PUBLIC

void AsyncRWLock::lock() {
    auto id = std::this_thread::get_id();

    if (id == this->id_) {
        this->r_count_++;
        return;
    }

    auto current = MutexBits{};
    auto desired = MutexBits{};

    int attempts = 10;

    desired.acquire_unique();

    while (!this->lock_.compare_exchange_strong(current, desired)) {
        if (attempts == 0) {
            this->lock_slow();
            break;
        }

        current = MutexBits{};
        attempts--;
    }

    this->id_ = id;
    this->r_count_ = 1;
}

void AsyncRWLock::lock_shared() {
    auto id = std::this_thread::get_id();
    auto current = this->lock_.load(std::memory_order_relaxed);
    MutexBits desired{};

    do {
        if (current.is_ulocked() && this->id_ != id) {
            this->lock_shared_slow(id);
            return;
        }

        desired = current;
        desired.inc_shared();
    } while (!this->lock_.compare_exchange_strong(current, desired));
}

void AsyncRWLock::unlock() {
    if (this->r_count_ > 1) {
        assert(this->id_ == std::this_thread::get_id());

        this->r_count_--;
        return;
    }

    auto current = this->lock_.load(std::memory_order_relaxed);
    MutexBits desired{};

    this->id_ = std::thread::id();
    this->r_count_ = 0;

    do {
        desired = current;
        desired.release_unique();
    } while (!this->lock_.compare_exchange_strong(current, desired, std::memory_order_acq_rel));

    if (this->pending_ > 0)
        OS_WAKE(&this->lock_);
}

void AsyncRWLock::unlock_shared() {
    auto current = this->lock_.load(std::memory_order_relaxed);
    MutexBits desired{};

    do {
        desired = current;
        desired.dec_shared();
    } while (!this->lock_.compare_exchange_strong(current, desired, std::memory_order_acq_rel));

    if (this->pending_ > 0)
        OS_WAKE(&this->lock_);
}
