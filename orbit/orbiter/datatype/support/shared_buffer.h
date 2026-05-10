// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_SUPPORT_SHARED_BUFFER_H_
#define ORBIT_ORBITER_DATATYPE_SUPPORT_SHARED_BUFFER_H_

#include <atomic>

#include <orbit/datatype.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

namespace orbiter {
    class Isolate;
}

namespace orbiter::datatype::support {
    /**
     * @brief Reference-counted, lock-protected raw byte buffer shared by one
     *        or more views.
     *
     * `SharedBuffer` is the storage backbone for byte-oriented types whose
     * views can overlap (Bytes today, possibly mutable string/bitvector
     * tomorrow). It is intentionally **not** an `OObject`: it is plain heap
     * memory managed by manual reference counting, opaque to the GC. Owners
     * (typically Bytes instances) are responsible for calling `Acquire` /
     * `Release` to keep the lifetime consistent.
     *
     * Concurrency model
     * -----------------
     * Multiple views may concurrently read the buffer. Mutating operations
     * (Enlarge, byte writes performed by the owner) require the unique side
     * of `rwlock`. Acquire-side fencing in `SharedBufferAcquire` ensures
     * that a new view never observes a buffer mid-`Enlarge`. The shared
     * lock is intentionally bypassed when the buffer is `frozen`: a frozen
     * buffer cannot be mutated, so there is no writer to synchronise with.
     *
     * Freezing
     * --------
     * A buffer can be frozen via `SharedBufferFreeze`. The flag is
     * monotonic: once true, it never goes back to false. Every view that
     * shares this buffer becomes read-only as a consequence — the only way
     * to obtain a writable copy is to allocate a new `SharedBuffer` and
     * copy the contents (handled by the owning type, not here).
     */
    struct SharedBuffer {
        /// Read/write lock guarding the buffer contents. Readers hold it
        /// shared during a single byte read; writers (Enlarge) hold it
        /// unique briefly while reallocating.
        sync::AsyncRWLock rwlock;

        /// Number of views that currently reference this buffer.
        /// The struct is destroyed when this drops to zero.
        std::atomic_uint counter;

        /// Monotonic frozen flag. Once true, never returns to false.
        /// Frozen buffers reject all mutation; owners must detach first.
        bool frozen;

        /// The raw bytes. May be null when `capacity == 0`.
        unsigned char *buffer;

        /// Total bytes available in `buffer`.
        MSize capacity;

        [[nodiscard]] bool IsFrozen() const noexcept {
            return this->frozen;
        }

        /// True when this buffer can be mutated in place — i.e. it is not
        /// frozen. The reference counter is intentionally NOT consulted:
        /// shared, non-frozen buffers are mutated in place and the change
        /// is observed by every view that points at this buffer. Owners
        /// that want to isolate their writes from co-owners must detach
        /// (allocate a private SharedBuffer and copy) before writing.
        [[nodiscard]] bool IsWritable() const noexcept {
            return !this->IsFrozen();
        }
    };

    /**
     * @brief Appends data to a `SharedBuffer` starting at a specified offset.
     *
     * This function appends a sequence of bytes to the buffer represented by
     * the `SharedBuffer` object. If the buffer is marked as frozen or the
     * starting offset exceeds the buffer's capacity, the operation fails.
     * Locking and synchronization are performed to ensure thread safety during
     * the append operation.
     *
     * Preconditions:
     * - The buffer must not be frozen.
     * - The starting offset (`start`) must be less than the buffer's capacity.
     *
     * @param isolate Pointer to the Isolate context, used for memory operations.
     * @param sb Pointer to the `SharedBuffer` to be appended to.
     * @param data Pointer to the input data to append.
     * @param start The starting offset within the buffer to begin the write.
     * @param length The number of bytes to append from the input data.
     * @return `true` if the append operation succeeds, `false` otherwise (e.g.,
     *         buffer is frozen, or resizing fails).
     */
    bool SharedBufferAppend(Isolate *isolate, SharedBuffer *sb, const unsigned char *data, MSize start, MSize length);

    bool SharedBufferAppendLocked(Isolate *isolate, SharedBuffer *sb, const unsigned char *data, MSize start,
                                  MSize length) noexcept;

    /**
     * @brief Grow the buffer to at least @p new_capacity bytes.
     *
     * Resizes the underlying allocation in place.
     * The unique side of `rwlock` is held for the duration so concurrent
     * readers see either the old or the new buffer, never a torn state.
     *
     * Co-owners (other views on the same buffer) automatically observe
     * the new capacity at their next access: they reference the buffer
     * by `(start, length)` indices, not by cached pointer offsets.
     *
     * Calling with @p new_capacity less than or equal to the current
     * capacity is a no-op that returns true.
     *
     * @param isolate       Allocator backing.
     * @param sb            The buffer to grow. Must be non-null.
     * @param new_capacity  Desired minimum capacity in bytes.
     *
     * @return true on success, false on allocation failure (in which case
     *         the buffer is left untouched).
     */
    bool SharedBufferEnlarge(Isolate *isolate, SharedBuffer *sb, MSize new_capacity);

    /**
     * @brief Increments the reference count of the given SharedBuffer.
     *
     * This function acquires ownership of the specified `SharedBuffer` by
     * incrementing its reference count. If the buffer is not frozen, it
     * synchronizes with any in-progress `Enlarge` operation by acquiring the
     * shared side of the lock. For frozen buffers, as they cannot be mutated,
     * the increment operation suffices without acquiring the lock.
     *
     * @param sb A pointer to the `SharedBuffer` instance whose reference count
     *           is to be incremented. Must not be null.
     * @return A pointer to the same `SharedBuffer` instance provided in the
     *         `sb` parameter.
     */
    SharedBuffer *SharedBufferAcquire(SharedBuffer *sb) noexcept;

    /**
     * @brief Allocate a fresh SharedBuffer with a private byte array.
     *
     * The new buffer starts with `counter = 1` (the caller is the first
     * owner) and the requested frozen flag.
     *
     * @param isolate   Allocator backing.
     * @param capacity  Initial capacity in bytes. May be 0; in that case
     *                  no byte array is allocated and `buffer` is null
     *                  until the first `SharedBufferEnlarge`.
     * @param frozen    Initial value of the frozen flag.
     *
     * @return Pointer to the new SharedBuffer, or nullptr on allocation failure.
     */
    SharedBuffer *SharedBufferNew(Isolate *isolate, MSize capacity, bool frozen);

    /**
     * @brief Mark the buffer as frozen.
     *
     * Frozen is monotonic: this function flips the flag from false to true
     * and is idempotent on subsequent calls. After freezing, every view
     * that shares this buffer becomes read-only — owners that want a
     * mutable copy must allocate a new `SharedBuffer` and copy bytes
     * across.
     *
     * Safe to call concurrently: the flag is an atomic store with release
     * semantics so subsequent acquires observe the freeze.
     *
     * @param sb The buffer to freeze. Must be non-null.
     */
    void SharedBufferFreeze(SharedBuffer *sb);

    /**
     * @brief Mark the buffer as frozen.
     */
    inline void SharedBufferFreezeLocked(SharedBuffer *sb) noexcept {
        sb->frozen = true;
    }

    /**
     * @brief Decrement the reference counter; destroy when it reaches zero.
     *
     * The last owner is responsible for tearing down both the byte array
     * and the struct itself, using @p isolate's allocator.
     *
     * @param isolate  Allocator backing.
     * @param sb       The shared buffer to release. Must be non-null.
     */
    void SharedBufferRelease(Isolate *isolate, SharedBuffer *sb);
}

#endif // !ORBIT_ORBITER_DATATYPE_SUPPORT_SHARED_BUFFER_H_
