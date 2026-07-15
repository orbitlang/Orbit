// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_SYNC_H_
#define ORBIT_ORBITER_SYNC_H_

#include <atomic>

#include <orbit/orbiter/fqueue.h>

namespace orbiter::sync {
    class Monitor {
        std::atomic_flag spinlock_ = ATOMIC_FLAG_INIT;
        std::atomic_uintptr_t lock_id_ = 0;

        uintptr_t locks = 0;

        FiberQueue<false> queue_{};

        void SpinLock();

        void SpinUnlock();

    public:
        /**
         * @brief Attempts to acquire the monitor lock for the given fiber.
         *
         * If the lock is successfully acquired, the fiber is granted access
         * and the lock count is incremented. If the lock is already held by
         * another fiber, the method will enqueue the requesting fiber and
         * return false; with @p can_block set to false the fiber is NOT
         * enqueued (no wakeup will ever be delivered) and the acquire simply
         * fails — used by synchronous VM re-entries, which cannot suspend.
         *
         * @param fiber A pointer to the fiber attempting to acquire the lock.
         * @param can_block Whether the fiber may be enqueued for a wakeup on
         *        contention.
         * @return true if the lock is successfully acquired by the fiber,
         *         false if the lock is held by another fiber (the requesting
         *         fiber is enqueued only when @p can_block is true).
         */
        bool Acquire(Fiber *fiber, bool can_block);

        /**
         * @brief Checks if the monitor instance can be safely destroyed.
         *
         * This method verifies the conditions required for destruction
         * of the monitor instance. It checks if the `lock_id_` is zero,
         * no locks are currently held (`locks` equals zero), and the
         * synchronized queue of fibers (`queue_`) is empty.
         *
         * @return true if the monitor instance is in a destructible state;
         *         false otherwise.
         */
        bool IsDestroyable() {
            return this->lock_id_ == 0 && this->locks == 0 && this->queue_.IsEmpty();
        }

        /**
         * @brief Releases the lock held by the current fiber in the monitor.
         *
         * Decrements the internal lock count for the monitor. If the lock count
         * remains greater than zero, the method simply unlocks the spinlock and exits.
         * Otherwise, the method signals that the monitor can be acquired by another fiber.
         * If there are fibers waiting in the monitor's queue, one of them is dequeued
         * and given ownership of the lock.
         *
         * The monitor employs a spinlock for synchronization when managing the lock count
         * and queue operations. Proper handling ensures no fiber starvation by transferring
         * ownership to the next fiber in queue before the current fiber exits.
         *
         */
        void Release();
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_SYNC_H_
