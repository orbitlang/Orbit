// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/bytes.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/datatype/byteview.h>

using namespace orbiter::datatype;

ByteView::ByteView(Isolate *isolate, const OObject *obj) noexcept : object_(obj), data_(nullptr), size_(0),
                                                                        valid_(false) {
    if (O_IS_OBJECT(obj)) {
        if (O_IS_TYPE(obj, InstanceType::STRING)) {
            // Strings are immutable: the buffer never moves, no lock needed.
            const auto *str = (const ORString *) obj;

            this->data_ = str->buffer;
            this->size_ = str->length;
            this->valid_ = true;

            return;
        }

        if (O_IS_TYPE(obj, InstanceType::BYTES)) {
            const auto *bytes = (const Bytes *) obj;
            auto *shared = bytes->shared;

            // A frozen buffer can never be mutated, so there is no writer to
            // synchronise with — skip the lock exactly like SharedBufferAcquire.
            // Otherwise hold the shared side for the view's lifetime so a
            // concurrent SharedBufferEnlarge cannot reallocate `buffer` and
            // leave us with a dangling pointer.
            if (!shared->IsFrozen())
                this->lock_ = std::shared_lock(shared->rwlock);

            this->data_ = shared->buffer + bytes->start;
            this->size_ = bytes->length;
            this->valid_ = true;

            return;
        }
    }

    ErrorSetWithObjType(isolate,
                        TypeError::Details[TypeError::ID],
                        "expected a Bytes or String, got '%s'",
                        nullptr,
                        obj);
}

bool ByteView::PinBuffer() const noexcept {
    if (!this->valid_ || !O_IS_TYPE(this->object_, InstanceType::BYTES))
        return false;

    ((const Bytes *) this->object_)->shared->Pin();

    return true;
}

void ByteView::UnpinBuffer() const noexcept {
    assert(this->valid_ && O_IS_TYPE(this->object_, InstanceType::BYTES));

    ((const Bytes *) this->object_)->shared->Unpin();
}
