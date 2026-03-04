// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_FPOOL_H_
#define ORBIT_ORBITER_FPOOL_H_

#include <orbit/orbiter/fqueue.h>

namespace orbiter {
    constexpr unsigned short kFiberPoolSize = 255; // Items

    class FiberPool {
        FiberQueue<> queue_;

        Isolate *isolate_;

        U32 pool_size_ = kFiberPoolSize;
        U32 stack_size_ = kMinStackSize;

        U64 stack_limit_ = kMaxStackSize;

    public:
        explicit FiberPool(Isolate *isolate, const I32 pool_size, const I32 stack_size,
                           const I64 stack_limit) noexcept : queue_(pool_size > 0 ? pool_size : kFiberPoolSize),
                                                             isolate_(isolate) {
            if (pool_size > 0)
                this->pool_size_ = pool_size;

            if (stack_size > 0)
                this->stack_size_ = stack_size;

            if (stack_limit > 0)
                this->stack_limit_ = stack_limit;
        }

        Fiber *NewFiber() noexcept {
            auto *fiber = this->queue_.Dequeue();

            if (fiber == nullptr)
                fiber = Fiber::New(this->isolate_, this->stack_size_, this->stack_limit_);
            else
                fiber->Reset();

            return fiber;
        }

        void DeleteFiber(Fiber *fiber) noexcept {
            if (!this->queue_.Enqueue(fiber))
                Fiber::Delete(fiber);
        }
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_FPOOL_H_
