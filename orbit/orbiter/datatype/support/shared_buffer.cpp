// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

#include <orbit/orbiter/memory/iallocator.h>

#include <orbit/orbiter/datatype/support/shared_buffer.h>

using namespace orbiter::datatype;

bool support::SharedBufferAppend(Isolate *isolate, SharedBuffer *sb, const unsigned char *data, const MSize start,
                                 const MSize length) {
    if (sb->frozen)
        return false;

    std::unique_lock _(sb->rwlock);

    return SharedBufferAppendLocked(isolate, sb, data, start, length);
}

bool support::SharedBufferAppendLocked(Isolate *isolate, SharedBuffer *sb, const unsigned char *data, const MSize start,
                                       const MSize length) noexcept {
    if (sb->frozen)
        return false;

    if (!SharedBufferEnlargeLocked(isolate, sb, start + length))
        return false;

    if (data != nullptr)
        memory::MemoryCopy(sb->buffer + start, data, length);

    return true;
}

bool support::SharedBufferEnlarge(Isolate *isolate, SharedBuffer *sb, const MSize new_capacity) noexcept {
    std::unique_lock _(sb->rwlock);

    return SharedBufferEnlargeLocked(isolate, sb, new_capacity);
}

bool support::SharedBufferEnlargeLocked(Isolate *isolate, SharedBuffer *sb, const MSize new_capacity) noexcept {
    assert(!sb->frozen && "SharedBufferEnlarge cannot grow a frozen buffer; detach first");

    if (sb->pin.load(std::memory_order_acquire) > 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 ValueError::Details[ValueError::Reason::IO_BUSY]);

        return false;
    }

    if (new_capacity <= sb->capacity)
        return true;

    memory::IsolateAllocator allocator(isolate);

    unsigned char *new_buffer;
    if (sb->buffer == nullptr)
        new_buffer = allocator.alloc<unsigned char>(new_capacity);
    else
        new_buffer = allocator.realloc(sb->buffer, new_capacity);

    if (new_buffer == nullptr)
        return false;

    sb->buffer = new_buffer;
    sb->capacity = new_capacity;

    return true;
}

support::SharedBuffer *support::SharedBufferAcquire(SharedBuffer *sb) noexcept {
    if (!sb->IsFrozen()) {
        // Synchronise with any in-flight Enlarge: that holds the unique
        // side of the lock while reallocating, so taking the shared side
        // here forces us to wait until the buffer is in a coherent state
        // before publishing a new owner.
        std::shared_lock _(sb->rwlock);

        sb->counter.fetch_add(1, std::memory_order_acq_rel);

        return sb;
    }

    // Frozen buffers cannot be mutated — the increment is enough.
    sb->counter.fetch_add(1, std::memory_order_acq_rel);

    return sb;
}

support::SharedBuffer *support::SharedBufferNew(Isolate *isolate, const MSize capacity, const bool frozen) {
    memory::IsolateAllocator allocator(isolate);

    auto *sb = allocator.alloc<SharedBuffer>(sizeof(SharedBuffer));
    if (sb == nullptr)
        return nullptr;

    // Allocate the byte array up front. Capacity 0 is allowed and yields a
    // null buffer — Enlarge will bring it into existence on first growth.
    sb->buffer = nullptr;
    sb->capacity = capacity;
    sb->frozen = frozen;

    if (capacity > 0) {
        sb->buffer = allocator.alloc<unsigned char>(capacity);
        if (sb->buffer == nullptr) {
            allocator.free(sb);

            return nullptr;
        }
    }

    new(&sb->rwlock) sync::AsyncRWLock();
    new(&sb->counter) std::atomic_uint(1);
    new(&sb->pin) std::atomic_uint(0);

    return sb;
}

void support::SharedBufferFreeze(SharedBuffer *sb) {
    std::unique_lock _(sb->rwlock);

    sb->frozen = true;
}

void support::SharedBufferRelease(Isolate *isolate, SharedBuffer *sb) {
    if (sb->counter.fetch_sub(1, std::memory_order_acq_rel) != 1)
        return;

    // Last owner — tear down.
    const memory::IsolateAllocator allocator(isolate);

    if (sb->buffer != nullptr)
        allocator.free(sb->buffer);

    sb->counter.~atomic();
    sb->rwlock.~AsyncRWLock();

    allocator.free(sb);
}
