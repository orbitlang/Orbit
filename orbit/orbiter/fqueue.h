// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_FQUEUE_H_
#define ORBIT_ORBITER_FQUEUE_H_

#include <cassert>

namespace orbiter {
    class Fiber;

    /**
     * @class FiberQueue
     *
     * FiberQueue is a thread-safe queue designed to manage Fiber objects. It handles
     * a maximum number of fibers and ensures proper synchronization during enqueueing
     * and dequeuing operations.
     */
    template<bool ThreadSafe = true>
    class FiberQueue {
        class NoopMutex {
        public:
            void lock() {
            }

            void unlock() {
            }
        };

        using MutexType = std::conditional_t<ThreadSafe, std::mutex, NoopMutex>;

        //                                            +----head
        //                                            v
        //           +--------+    +--------+    +--------+
        //           |        |    |        |    |        |
        // tail ---> |  obj3  +--->|  obj2  +--->|  obj1  |
        //           |        |    |        |    |        |
        //           +--------+    +--------+    +--------+

        MutexType lock_;

        Fiber *head_ = nullptr;
        Fiber *tail_ = nullptr;

        U32 count_ = 0;
        U32 max_;

    public:
        explicit FiberQueue(const U32 max_length) noexcept : max_(max_length) {
        }

        explicit FiberQueue() noexcept : max_(0) {
        }

        ~FiberQueue() {
            assert(this->count_ == 0);
        }

        /**
         * @brief Adds a Fiber object to the end of the queue.
         *
         * This method attempts to enqueue a Fiber into the FiberQueue. If the queue's
         * maximum capacity is not reached, the Fiber is added successfully, and the
         * internal count is updated. If a nullptr is passed as the argument, the method
         * will return true without enqueuing anything. The operation is thread-safe.
         *
         * @param fiber A pointer to the Fiber object to be added to the queue.
         * @return true if the Fiber was successfully enqueued or if `fiber` is nullptr;
         *         false if the queue is at its maximum capacity and cannot accept new
         *         fibers.
         */
        bool Enqueue(Fiber *fiber) noexcept;

        /**
         * @brief Inserts a Fiber object at the head of the queue.
         *
         * This method adds the specified Fiber to the front of the queue. If the queue
         * has reached its maximum capacity (if a limit is set), the insertion fails.
         * Proper synchronization is ensured to maintain thread safety.
         *
         * @param fiber A pointer to the Fiber object to be inserted into the queue. If
         *              the pointer is nullptr, the method will simply return true without
         *              performing any modifications.
         * @return True if the Fiber object is successfully inserted, false if the
         *         operation fails due to capacity constraints.
         */
        bool InsertHead(Fiber *fiber) noexcept;

        /**
         * @brief Determines if the FiberQueue is empty.
         *
         * This method checks whether the queue currently contains any fibers.
         * The operation is thread-safe, ensuring proper synchronization.
         *
         * @return True if the FiberQueue is empty, otherwise false.
         */
        [[nodiscard]] bool IsEmpty() noexcept {
            std::unique_lock _(this->lock_);
            return this->count_ == 0;
        }

        /**
         * @brief Removes and returns the Fiber object at the front of the queue.
         *
         * This method is thread-safe and ensures proper synchronization during
         * the dequeue operation. If the queue is empty, it will return nullptr.
         *
         * @return A pointer to the Fiber object dequeued from the front of the queue,
         *         or nullptr if the queue is empty.
         */
        Fiber *Dequeue() noexcept;

        /**
         * @brief Removes and returns a fiber from this queue after attempting to steal fibers
         * from another FiberQueue.
         *
         * This method first tries to steal fibers from the `other` FiberQueue if it
         * contains at least `min_length` fibers. If fibers are successfully stolen,
         * this method then dequeues one fiber from this queue.
         *
         * @param min_length The minimum number of fibers the `other` queue must contain
         *                   to allow for stealing.
         * @param other The FiberQueue from which fibers may be stolen if the condition
         *              is met.
         * @return A pointer to the dequeued Fiber if stealing and dequeuing are successful;
         *         nullptr if no Fiber was dequeued.
         *
         * @note This operation is thread-safe.
         */
        Fiber *StealDequeue(const U16 min_length, FiberQueue &other) noexcept {
            if (this->Steal(min_length, other) > 0)
                return this->Dequeue();

            return nullptr;
        }

        /**
         * @brief Steals fibers from another FiberQueue and appends them to this queue.
         *
         * This operation is thread-safe and will try to steal approximately half
         * of the fibers from the `other` queue and add them to this queue. The function
         * respects a minimum length requirement; if the `other` queue has fewer fibers
         * than the specified `min_length`, no fibers are stolen.
         *
         * @param min_length The minimum number of fibers the `other` queue must contain for stealing to occur.
         * @param other The FiberQueue from which fibers will be stolen.
         * @return The number of fibers stolen and appended to this queue. Returns 0 if
         *         the minimum length condition is not met or the `other` queue is empty.
         *
         * @note The function assumes that `this` queue and `other` queue are distinct
         *       and will assert if they are the same.
         * @note The operation ensures thread-safety by locking both queues.
         */
        U32 Steal(U16 min_length, FiberQueue &other) noexcept;

        /**
         * @brief Removes the given fiber from the queue, updating the fiber queue's state as required.
         *
         * If the specified fiber pointer is null, the method performs no operation.
         *
         * @param fiber A pointer to the Fiber object that needs to be removed from the queue.
         */
        void Relinquish(Fiber *fiber) noexcept;
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_FQUEUE_H_
