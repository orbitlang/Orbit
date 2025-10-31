// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DEFER_H_
#define ORBIT_ORBITER_DEFER_H_

#include <orbit/datatype.h>

namespace orbiter {
    constexpr unsigned short kDeferPoolMaxSize = 128; // Items

    struct Defer {
        Defer *next;

        datatype::Function *func;

        PtrSize key;

        PtrSize argc;

        PtrSize r10;
        PtrSize r11;
        PtrSize r12;
        PtrSize SP;
    };

    class DeferStack {
        Defer *stack_ = nullptr;

    public:
        [[nodiscard]] bool IsEmpty() const noexcept {
            return this->stack_ == nullptr;
        }

        Defer *Pop(const PtrSize key) {
            auto *defer = this->stack_;

            if (defer != nullptr && defer->key == key) {
                this->stack_ = defer->next;

                defer->next = nullptr;

                return defer;
            }

            return nullptr;
        }

        void Push(Defer *defer, const PtrSize key) {
            defer->key = key;

            defer->next = this->stack_;
            this->stack_ = defer;
        }
    };

    class DeferPool {
        std::mutex lock_;

        DeferStack stack_;

        Isolate *isolate_;

        U32 pool_limit_ = kDeferPoolMaxSize;
        U32 pool_size_ = 0;

    public:
        explicit DeferPool(Isolate *isolate) noexcept : isolate_(isolate) {
        }

        Defer *NewDefer() noexcept {
            std::unique_lock _(this->lock_);

            auto *defer = this->stack_.Pop(0);
            if (defer != nullptr) {
                this->pool_size_ -= 1;

                return defer;
            }

            memory::IsolateAllocator allocator(this->isolate_);
            return allocator.alloc<Defer>(sizeof(Defer));
        }

        void DeleteDefer(Defer *defer) {
            O_DECREF(defer->func);

            std::unique_lock _(this->lock_);

            if (this->pool_size_ < this->pool_limit_) {
                memory::MemoryZero(defer, sizeof(Defer));

                this->stack_.Push(defer, 0);

                this->pool_size_ += 1;

                return;
            }

            const memory::IsolateAllocator allocator(this->isolate_);
            allocator.free(defer);
        }
    };
}

#endif // !ORBIT_ORBITER_DEFER_H_
