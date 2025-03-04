// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/obase.h>

#include <orbit/orbiter/memory/refcount.h>

using namespace orbiter::datatype;
using namespace orbiter::memory;

RCObject RefCount::GetObjectBase() {
    const auto obj = (RCObject) this;
    assert(((void *) obj == &(obj->head_.ref_count_)) && "RefCount must be FIRST field in OObject structure!");
    return obj;
}

SideTable *RefCount::AllocOrGetSideTable() {
    auto current = this->bits_.load(std::memory_order_seq_cst);

    if (!RC_HAVE_INLINE_COUNTER(current))
        return RC_GET_SIDETABLE(current);

    const auto obj = this->GetObjectBase();

    assert(obj->head_.type_ != nullptr);

    const auto isolate = O_GET_ISOLATE(obj);

    IsolateAllocator allocator(isolate);
    auto *side = allocator.alloc<SideTable>(sizeof(SideTable));
    if (side == nullptr)
        return nullptr;

    side->strong.store(RC_INLINE_GET_COUNT(current));
    side->weak.store(1);
    side->isolate = isolate;
    side->object = obj;

    auto desired = (uintptr_t) side;

    if (RC_CHECK_IS_GCOBJ(current))
        desired = RC_SETBIT_GC(desired);

    do {
        if (!RC_HAVE_INLINE_COUNTER(current)) {
            allocator.free(side);

            side = RC_GET_SIDETABLE(current);

            break;
        }

        side->strong = RC_INLINE_GET_COUNT(current);
    } while (!this->bits_.compare_exchange_weak(current, desired,
                                                std::memory_order_seq_cst,
                                                std::memory_order_seq_cst));
    return side;
}

bool RefCount::DecStrong(uintptr_t *out) {
    auto current = *((uintptr_t *) &this->bits_);
    uintptr_t desired = {};

    do {
        desired = current;

        if (!RC_HAVE_INLINE_COUNTER(desired)) {
            const auto side = RC_GET_SIDETABLE(desired);

            if (out != nullptr)
                *out = desired;

            if (side->strong.fetch_sub(1) == 1) {
                // OObject can be destroyed
                if (side->weak.fetch_sub(1) == 1) {
                    // No weak ref! SideTable can be destroyed
                    const IsolateAllocator allocator(side->isolate);
                    allocator.free(side);
                }
                return true;
            }
            return false;
        }

        desired = RC_INLINE_DEC(desired);
    } while (!this->bits_.compare_exchange_weak(current, desired,
                                                std::memory_order_release,
                                                std::memory_order_acquire));

    auto release = RC_CHECK_INLINE_ZERO(desired);

    if (out != nullptr)
        *out = desired;

    return release;
}

bool RefCount::DecWeak() const {
    auto current = *((uintptr_t *) &this->bits_);
    assert(!RC_HAVE_INLINE_COUNTER(current));

    auto side = RC_GET_SIDETABLE(current);
    auto weak = side->weak.fetch_sub(1);

    if (weak == 1) {
        const IsolateAllocator allocator(side->isolate);
        allocator.free(side);
    }

    return weak <= 2;
}

bool RefCount::HaveSideTable() const {
    auto current = *((uintptr_t *) &this->bits_);
    return !RC_HAVE_INLINE_COUNTER(current);
}

bool RefCount::IncStrong() {
    auto current = *((uintptr_t *) &this->bits_);
    uintptr_t desired = {};

    do {
        desired = current;

        if (!RC_HAVE_INLINE_COUNTER(desired)) {
            auto *side = RC_GET_SIDETABLE(desired);
            auto s_fetch = side->strong.fetch_add(1);

            assert(s_fetch != 0);

            return true;
        }

        desired = RC_INLINE_INC(desired);

        if (RC_CHECK_INLINE_OVERFLOW(desired)) {
            // Inline counter overflow
            auto *side = this->AllocOrGetSideTable();
            if (side == nullptr)
                return false;

            side->strong += 1;
            return true;
        }
    } while (!this->bits_.compare_exchange_weak(current, desired,
                                                std::memory_order_release,
                                                std::memory_order_acquire));

    return true;
}

RCObject RefCount::GetObject() {
    auto current = this->bits_.load(std::memory_order_seq_cst);

    if (RC_HAVE_INLINE_COUNTER(current)) {
        this->IncStrong();
        return this->GetObjectBase();
    }

    auto side = RC_GET_SIDETABLE(current);
    auto strong = *((uintptr_t *) &side->strong);
    uintptr_t desired;

    do {
        desired = strong + 1;
        if (desired == 1)
            return nullptr;
    } while (side->strong.compare_exchange_weak(strong, desired, std::memory_order_seq_cst));

    return side->object;
}

uintptr_t RefCount::IncWeak() {
    auto side = this->AllocOrGetSideTable();
    if (side != nullptr) {
        side->weak += 1;
        return (uintptr_t) side;
    }

    return 0;
}

uintptr_t RefCount::GetStrongCount() const {
    const auto current = this->bits_.load(std::memory_order_seq_cst);

    if (RC_HAVE_INLINE_COUNTER(current))
        return RC_INLINE_GET_COUNT(current);

    return RC_GET_SIDETABLE(current)->strong;
}

uintptr_t RefCount::GetWeakCount() const {
    const auto current = this->bits_.load(std::memory_order_seq_cst);

    if (!RC_HAVE_INLINE_COUNTER(current))
        return RC_GET_SIDETABLE(current)->weak;

    return 0;
}
