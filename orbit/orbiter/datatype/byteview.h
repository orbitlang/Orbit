// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_BYTEVIEW_H_
#define ORBIT_ORBITER_DATATYPE_BYTEVIEW_H_

#include <shared_mutex>

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

namespace orbiter {
    class Isolate;
}

namespace orbiter::datatype {
    /**
     * @brief RAII read-access scope over the raw bytes of a String or Bytes.
     *
     * Centralises the "give me `(pointer, length)` for this text-ish object"
     * pattern that several native/builtin call sites need, and makes it
     * **safe** under the runtime's concurrency model:
     *
     *   - **String** — strings are immutable; their `buffer` never moves.
     *     The view simply points at it, no lock is taken.
     *   - **Bytes** — the backing `SharedBuffer` can be reallocated in place
     *     by a concurrent `SharedBufferEnlarge`. The view therefore holds the
     *     **shared (read) side** of `SharedBuffer::rwlock` for its whole
     *     lifetime, so the pointer it hands out cannot dangle mid-read. A
     *     frozen buffer can never be mutated, so the lock is skipped for it
     *     (mirroring `SharedBufferAcquire`).
     *   - **Anything else** — a `TypeError` panic is set and `Ok()` returns
     *     false. Do not touch `Data()` in that case.
     *
     * The view is read-only: use `BytesWriteGuard` when you need to mutate a
     * Bytes. Like that guard it is non-copyable and non-movable — construct it
     * directly at the point of use and let it expire there.
     *
     * @example
     *     ByteView view(isolate, argv[0]);
     *     if (!view)
     *         return {};  // panic already set
     *
     *     ::write(fd, view.Data(), view.Size());
     */
    class ByteView {
        /// Engaged only for a non-frozen Bytes; default-constructed (and thus
        /// inert) for String and frozen Bytes.
        std::shared_lock<sync::AsyncRWLock> lock_;

        /// The object being viewed, kept for the pin helpers below.
        const OObject *object_;

        const unsigned char *data_;

        MSize size_;

        bool valid_;

    public:
        /**
         * @brief Build a read view over @p obj.
         *
         * On success `Data()` / `Size()` describe the object's raw bytes and,
         * for a non-frozen Bytes, the shared lock is held. On failure the
         * panic is set and `Ok()` returns false.
         *
         * @param isolate Owning isolate, used to raise the type error.
         * @param obj     String or Bytes to view. Any other value fails.
         *
         * @panic TypeError When @p obj is neither a String nor a Bytes.
         */
        ByteView(Isolate *isolate, const OObject *obj) noexcept;

        ByteView(const ByteView &) = delete;

        ByteView(ByteView &&) = delete;

        ByteView &operator=(const ByteView &) = delete;

        ByteView &operator=(ByteView &&) = delete;

        [[nodiscard]] bool Ok() const noexcept {
            return this->valid_;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return this->valid_;
        }

        /// First byte of the view. May be null when `Size()` is 0; valid only
        /// while the view is alive AND `Ok()` is true.
        [[nodiscard]] const unsigned char *Data() const noexcept {
            return this->data_;
        }

        /// Number of readable bytes.
        [[nodiscard]] MSize Size() const noexcept {
            return this->size_;
        }

        /**
         * @brief Fix the buffer behind the view in memory, so `Data()` stays
         *        valid after the view is released.
         *
         * Call it while the view still holds its lock, i.e. before Release or
         * destruction: that is what makes the pin race-free against Enlarge.
         * Meant for reads that outlive the view, such as an asynchronous write.
         *
         * Only a Bytes owns a buffer that can move. A String, whose bytes are
         * immutable and never reallocated, has nothing to pin.
         *
         * @return true when a pin was taken, which must be matched by exactly
         *         one Unpin; false when there was nothing to pin.
         */
        [[nodiscard]] bool PinBuffer() const noexcept;

        /**
         * @brief Drop a pin taken with PinBuffer, while the view is still alive.
         *
         * Once the view is gone, release the pin through `SharedBuffer::Unpin`
         * directly (e.g. from the on_resume of the operation).
         */
        void UnpinBuffer() const noexcept;

        /// Drop the read lock (if any) early. After this the data pointer must
        /// no longer be dereferenced for a non-frozen Bytes.
        void Release() noexcept {
            if (this->lock_.owns_lock())
                this->lock_.unlock();
        }
    };
}

#endif // !ORBIT_ORBITER_DATATYPE_BYTEVIEW_H_
