// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/rguard.h>

using namespace orbiter::datatype;

bool RGuard::Push(Isolate *isolate, OObject *object) {
    if (!this->Reserve(isolate, 1))
        return false;

    this->stack_[this->length_++] = object;

    return true;
}

bool RGuard::Reserve(Isolate *isolate, const MSize size) {
    memory::IsolateAllocator allocator(isolate);

    if (this->stack_ == nullptr) {
        this->stack_ = allocator.alloc<OObject *>(size);
        if (this->stack_ == nullptr)
            return false;

        this->capacity_ = size;
        this->length_ = 0;

        return true;
    }

    if (this->capacity_ - this->length_ >= size)
        return true;

    const auto tmp = allocator.realloc<OObject *>(this->stack_, this->capacity_ * 2);
    if (tmp == nullptr)
        return false;

    this->stack_ = tmp;
    this->capacity_ += size;

    return true;
}

void RGuard::Cleanup(Isolate *isolate) {
    const memory::IsolateAllocator allocator(isolate);

    allocator.free(this->stack_);

    this->stack_ = nullptr;
    this->capacity_ = 0;
    this->length_ = 0;
}

void RGuard::Pop() {
    if (this->length_ > 0)
        this->length_ -= 1;
}

// *********************************************************************************************************************
// ReprGuard
// *********************************************************************************************************************

ReprGuard::ReprGuard(OObject *self) {
    auto *fiber = Fiber::Current();

    assert(fiber != nullptr);

    auto *guard = &fiber->rguard;

    for (auto i = 0; i < guard->length_; i++) {
        if (guard->stack_[i] == self) {
            this->cyclic_ = true;

            break;
        }
    }

    if (!this->cyclic_)
        this->error_ = !guard->Push(fiber->isolate, self);
}

ReprGuard::~ReprGuard() {
    auto *fiber = Fiber::Current();

    assert(fiber != nullptr);

    if (!this->cyclic_ && !this->error_)
        fiber->rguard.Pop();
}
