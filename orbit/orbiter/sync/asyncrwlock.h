// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_SYNC_ASYNCRWLOCK_H_
#define ORBIT_ORBITER_SYNC_ASYNCRWLOCK_H_

#include <atomic>
#include <cassert>
#include <thread>

namespace orbiter::sync {
    using MutexWord = unsigned int;

    class MutexBits {
        MutexWord bits_{};

    public:
        [[nodiscard]] bool is_ulocked() const {
            return (this->bits_ & 0x01) == 1;
        }

        [[nodiscard]] MutexWord value() const {
            return this->bits_;
        }

        void acquire_unique() {
            this->bits_ |= 1;
        }

        void dec_shared() {
            assert((this->bits_ >> 1) > 0);

            this->bits_ -= 0x01 << 1;
        }

        void inc_shared() {
            this->bits_ += 0x01 << 1;
        }

        void release_unique() {
            this->bits_ &= ~1;
        }
    };

    /**
     * @brief A read-write lock designed for use in an asynchronous, fiber-based runtime.
     *
     * Standard library mutexes enforce a strict ownership model: the thread that acquires
     * the lock must be the same thread that releases it. This constraint is incompatible
     * with a fiber scheduler, where a fiber can be suspended on one OS thread and resumed
     * on a completely different one.
     *
     * Consider the following scenario:
     *   - Thread A acquires the lock on behalf of Fiber F and initiates an asynchronous
     *     I/O operation (e.g. a socket recv). Because no data is immediately available,
     *     the VM suspends Fiber F and inserts it into the event loop, keeping the lock held.
     *   - Thread B, which drives the event loop, later completes the I/O operation and
     *     resumes Fiber F. At that point, Thread B must release the lock on behalf of
     *     Fiber F — a cross-thread unlock that standard mutexes explicitly forbid.
     *
     * AsyncRWLock removes this ownership restriction and additionally provides:
     *
     *   - Cross-thread unlock: any thread may release a lock regardless of which thread
     *     originally acquired it, making the lock safe across fiber suspension points.
     *
     *   - Recursive unique locking: a thread that already holds the unique lock may call
     *     lock() again without deadlocking. Each call to lock() must be matched by a
     *     corresponding call to unlock(), and all unlocks must be issued by the same thread
     *     that performed the recursive acquisitions.
     *
     *   - Shared (read) locking: multiple threads may hold the shared lock concurrently,
     *     enabling parallel reads. A shared lock is blocked only while a unique lock is
     *     held by a different thread. The unique lock holder may also acquire the shared
     *     lock without deadlocking.
     *
     * Implementation notes:
     *   The lock state is encoded in a single atomic word: bit 0 represents the unique
     *   lock flag; bits 1–31 hold the active shared reader count. Acquisition is attempted
     *   via CAS in a short spin loop before falling back to an OS-level wait primitive
     *   (futex on Linux, __ulock on Darwin, WaitOnAddress on Windows), ensuring the
     *   implementation does not burn CPU cycles under sustained contention.
     */
    class AsyncRWLock {
        std::atomic<MutexBits> lock_{};
        std::atomic<MutexWord> pending_{};

        std::thread::id id_{};

        MutexWord r_count_{};

        void lock_shared_slow(std::thread::id id);

        void lock_slow();

    public:
        void lock();

        void lock_shared();

        void unlock();

        void unlock_shared();
    };
} // namespace orbiter::sync

#endif // !ORBIT_ORBITER_SYNC_ASYNCRWLOCK_H_
