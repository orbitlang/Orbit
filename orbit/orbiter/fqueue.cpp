// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fqueue.h>

using namespace orbiter;

bool FiberQueue::Enqueue(Fiber *fiber) noexcept {
    if (fiber == nullptr)
        return true;

    std::unique_lock _(this->lock_);

    if (this->max_ > 0 && (this->count_ + 1 >= this->max_))
        return false;

    fiber->queue.next = this->tail_;
    fiber->queue.prev = nullptr;

    if (this->tail_ == nullptr)
        this->head_ = fiber;
    else
        this->tail_->queue.prev = fiber;

    this->tail_ = fiber;

    this->count_++;

    return true;
}

bool FiberQueue::InsertHead(Fiber *fiber) noexcept {
    if (fiber == nullptr)
        return true;

    std::unique_lock _(this->lock_);

    if (this->max_ > 0 && (this->count_ + 1 >= this->max_))
        return false;

    if (this->head_ == nullptr) {
        this->head_ = fiber;
        this->tail_ = fiber;

        this->count_++;

        return true;
    }

    this->head_->queue.next = fiber;
    fiber->queue.prev = this->head_;

    this->head_ = fiber;

    this->count_++;

    return true;
}

bool FiberQueue::IsEmpty() noexcept {
    std::unique_lock _(this->lock_);
    return this->count_ == 0;
}

Fiber *FiberQueue::Dequeue() noexcept {
    std::unique_lock lock(this->lock_);

    auto *ret = this->head_;
    if (ret != nullptr) {
        this->head_ = ret->queue.prev;

        if (this->head_ == nullptr)
            this->tail_ = nullptr;

        this->count_--;
    }

    return ret;
}

Fiber *FiberQueue::StealDequeue(U16 min_length, FiberQueue &other) noexcept {
    if (this->Steal(min_length, other) > 0)
        return this->Dequeue();

    return nullptr;
}

U32 FiberQueue::Steal(U16 min_length, FiberQueue &other) noexcept {
    assert(this != &other);

    std::unique_lock lock(this->lock_);
    std::unique_lock lock_other(other.lock_);

    Fiber *mid = other.tail_; // Mid element pointer
    Fiber *last = other.head_; // Pointer to last element in queue
    Fiber *mid_prev = nullptr;

    // Check target queue minimum length
    if (other.count_ == 0 || other.count_ < min_length)
        return 0;

    // Steal half queue
    unsigned int counter = 0;

    for (Fiber *cursor = other.tail_; cursor != nullptr; cursor = cursor->queue.next) {
        last = cursor;

        if (counter & 1u) {
            mid_prev = mid;
            mid = mid->queue.next;
        }

        counter++;
    }

    auto grab_len = (other.count_ / 2) + (other.count_ & 1u);

    // Other contains a single fiber.
    if (other.tail_ == other.head_)
        other.tail_ = nullptr;

    // This code transfers the second half of the queue from the source queue to the current one,
    // leaving the first half in the original queue.
    other.head_ = mid_prev;
    if (mid_prev != nullptr)
        mid_prev->queue.next = nullptr;
    other.count_ -= grab_len;

    mid->queue.prev = nullptr;

    if (this->tail_ == nullptr) {
        this->tail_ = mid;
        this->head_ = last;
    } else {
        this->tail_->queue.prev = last;
        last->queue.next = this->tail_;

        this->tail_ = mid;
    }

    this->count_ += grab_len;

    return grab_len;
}

void FiberQueue::Relinquish(Fiber *fiber) noexcept {
    if (fiber == nullptr)
        return;

    std::unique_lock lock(this->lock_);

    if (fiber->queue.prev != nullptr)
        fiber->queue.prev->queue.next = fiber->queue.next;

    if (fiber->queue.next != nullptr)
        fiber->queue.next->queue.prev = fiber->queue.prev;

    if (this->tail_ == fiber)
        this->tail_ = fiber->queue.next;

    if (this->head_ == fiber)
        this->head_ = fiber->queue.prev;

    this->count_--;
}
