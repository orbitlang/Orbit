// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/memory/memory.h>

#include <orbit/liftoff/scanner/sbuffer.h>

using namespace liftoff::scanner;

StoreBuffer::~StoreBuffer() {
    this->allocator_.free(this->buffer_);
}

bool StoreBuffer::Enlarge(const size_t increase) {
    if (this->buffer_ == nullptr) {
        this->buffer_ = this->allocator_.alloc<unsigned char>(increase + 1);
        if (this->buffer_ == nullptr)
            return false;

        this->cursor_ = this->buffer_;
        this->end_ = this->buffer_ + increase;
    }

    const size_t available = this->end_ - this->cursor_; // cursor_ <= end_ invariant

    if (available < increase) {
        const size_t used = this->cursor_ - this->buffer_;
        const size_t capacity = this->end_ - this->buffer_;
        const size_t needed = used + increase;

        // Grow geometrically so building a token costs amortized O(n) instead of
        // O(n^2) — Stratum's realloc copies on every grow and its size classes
        // are 8-byte granular, so a fixed +8 step would copy on each call.
        // GetBuffer trims the slack back to the exact size on hand-off.
        size_t newcap = capacity * 2;
        if (newcap < needed)
            newcap = needed;

        const auto tmp = this->allocator_.realloc(this->buffer_, newcap + 1);
        if (tmp == nullptr)
            return false;

        this->cursor_ = tmp + used;
        this->buffer_ = tmp;
        this->end_ = tmp + newcap;
    }

    return true;
}

bool StoreBuffer::PutChar(const unsigned char chr) {
    do {
        if (this->cursor_ < this->end_) {
            *this->cursor_ = chr;

            this->cursor_++;

            return true;
        }

        if (!this->Enlarge(8))
            return false;
    } while (true);
}

bool StoreBuffer::PutCharRepeat(unsigned char chr, int n) {
    if (!this->Enlarge(n))
        return false;

    while (n > 0) {
        *this->cursor_ = chr;
        this->cursor_++;
        n--;
    }

    return true;
}

bool StoreBuffer::PutString(const unsigned char *str, size_t length) {
    if (!this->Enlarge(length + 8))
        return false;

    this->cursor_ = (unsigned char *) orbiter::memory::MemoryCopy(this->cursor_, str, length);

    return true;
}

unsigned int StoreBuffer::GetBuffer(unsigned char **buffer) {
    if (this->buffer_ == nullptr) {
        *buffer = nullptr;

        return 0;
    }

    const auto length = (unsigned int) (this->cursor_ - this->buffer_);

    assert(this->cursor_ < (this->end_ + 1));

    *this->cursor_ = '\0';

    // Shrink-to-fit: the buffer becomes the token's permanent storage, so trim
    // the geometric over-allocation back to the exact content size (+1 NUL). On
    // realloc-down failure Stratum keeps the original block intact, so fall back
    // to it.
    const auto fit = this->allocator_.realloc(this->buffer_, length + 1);
    *buffer = fit != nullptr ? fit : this->buffer_;

    this->buffer_ = nullptr;
    this->cursor_ = nullptr;
    this->end_ = nullptr;

    return length;
}

void StoreBuffer::Clear() noexcept {
    this->cursor_ = this->buffer_;
}
