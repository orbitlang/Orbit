// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_BYTES_H_
#define ORBIT_ORBITER_DATATYPE_BYTES_H_

#include <mutex>

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/datatype/support/shared_buffer.h>

namespace orbiter::datatype {
    struct Bytes {
        OROBJ_HEAD;

        /// Reference-counted backing storage. Never null on a live Bytes.
        support::SharedBuffer *shared;

        /// Index of the first byte of this view inside `shared->buffer`.
        MSize start;

        /// Number of bytes visible through this view.
        MSize length;

        MSize hash;
    };

    using HBytes = Handle<Bytes>;

    /**
     * @brief RAII write-access scope for a Bytes view.
     *
     * Centralises the three things every "write into a Bytes" call site
     * must get right:
     *   1. validate that the requested range `[offset, offset + length)`
     *      fits inside the view's current length;
     *   2. validate that the underlying `SharedBuffer` is not frozen;
     *   3. hold `SharedBuffer::rwlock` in unique mode for the duration of
     *      the write, so a concurrent `SharedBufferEnlarge` (which can
     *      reallocate `buffer`) cannot leave the writer with a dangling
     *      pointer.
     *
     * Construction performs all checks and locks the buffer. On failure
     * (out-of-bounds, frozen) the isolate panic is set and `ok()` returns
     * false — do not touch `data()` in that case. Destruction releases
     * the lock automatically.
     *
     * @example
     *     BytesWriteGuard guard(buf, offset, length);
     *     if (!guard)
     *         return {};  // panic already set
     *
     *     std::memcpy(guard.data(), src, length);
     */
    class BytesWriteGuard {
        std::unique_lock<sync::AsyncRWLock> lock_;

        unsigned char *data_;
    public:
        /**
         * @brief Acquire write access on `[offset, offset + length)` of @p bytes.
         *
         * On success the unique side of `bytes->shared->rwlock` is held and
         * `data()` points at `bytes->shared->buffer + bytes->start + offset`.
         * On failure the panic is set, the lock is released, and `data()` returns nullptr.
         *
         * @param bytes    Target view. Must be non-null.
         * @param offset   Start of the writable range, relative to the view.
         * @param length   Number of writable bytes requested.
         *
         * @panic ValueError  When @p bytes is frozen or the requested range
         *                    exceeds the view's current length.
         */
        BytesWriteGuard(Bytes *bytes, MSize offset, MSize length) noexcept;

        BytesWriteGuard(const BytesWriteGuard &) = delete;
        BytesWriteGuard(BytesWriteGuard &&) = delete;

        BytesWriteGuard &operator=(const BytesWriteGuard &) = delete;
        BytesWriteGuard &operator=(BytesWriteGuard &&) = delete;

        [[nodiscard]] bool ok() const noexcept {
            return this->data_ != nullptr;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return this->ok();
        }

        /// Pointer to the first byte of the writable range. Valid only
        /// while the guard is alive AND `ok()` is true.
        [[nodiscard]] unsigned char *data() const noexcept {
            return this->data_;
        }
    };

    bool BytesAppend(Bytes *bytes, const Bytes *other) noexcept;

    bool BytesAppendData(Bytes *bytes, const unsigned char *buffer, MSize length) noexcept;

    /**
     * @brief Substring containment: is @p needle a contiguous subrange of @p haystack?
     *
     * An empty needle is contained in every Bytes (returns true).
     */
    bool BytesContains(const Bytes *haystack, const Bytes *needle) noexcept;

    /**
     * @brief Byte-wise equality.
     *
     * Returns true if @p left and @p right have the same length and
     * identical bytes. Identity (same pointer) is fast-pathed.
     */
    bool BytesEqual(const Bytes *left, const Bytes *right) noexcept;

    /**
     * @brief Set up additional features and properties for the specified type
     *
     * This function enriches the previously created type with various functionalities.
     * It typically performs the following tasks:
     * - Adds default methods to the type
     * - Adds required properties to the type
     *
     * This function is called immediately after the type's Init function to complete its setup.
     *
     * @param self Pointer to TypeInfo created by %type%Init call
     *
     * @return true if setup was successful, false otherwise
     */
    bool BytesTypeSetup(TypeInfo *self) noexcept;

    /**
     * @brief Allocate a Bytes containing a copy of @p buffer.
     *
     * Copies @p length bytes from @p buffer into a freshly allocated
     * SharedBuffer. The source pointer is not retained.
     *
     * @param isolate  Owning isolate.
     * @param buffer   Source bytes. May be null only if @p length is 0.
     * @param length   Number of bytes to copy.
     * @param frozen   If true the returned Bytes is frozen on creation.
     *
     * @return Handle to the new Bytes, or empty on allocation failure.
     */
    HBytes BytesNew(Isolate *isolate, const unsigned char *buffer, MSize length, bool frozen) noexcept;

    /**
     * @brief Allocate an empty Bytes with at least @p capacity bytes of room.
     *
     * Length is initialised to 0; the byte storage is allocated but its
     * contents are indeterminate. Use this when you intend to fill the
     * buffer in-place via the public API.
     *
     * @param isolate   Owning isolate.
     * @param capacity  Initial capacity in bytes. May be 0; the storage
     *                  will be lazily grown by `BytesAppend*`.
     * @param frozen    If true the returned Bytes is frozen on creation.
     *
     * @return Handle to the new Bytes, or empty on allocation failure.
     */
    HBytes BytesNew(Isolate *isolate, MSize capacity, bool frozen) noexcept;

    /**
     * @brief Allocate a zero-copy slice over an existing Bytes.
     *
     * The returned Bytes shares the same SharedBuffer as @p src, with the
     * window `(src.start + start, length)`. Mutations through the new
     * slice are visible to @p src and vice-versa, unless one side first
     * detaches.
     *
     * If @p src's SharedBuffer is frozen, the slice inherits the frozen
     * status (the flag lives on the SharedBuffer).
     *
     * @param src      Source Bytes.
     * @param start    Byte offset within @p src at which the slice starts.
     *                 `start + length <= src->length` is required.
     * @param length   Length of the slice in bytes.
     *
     * @return Handle to the new Bytes, or empty on allocation failure or
     *         out-of-range bounds.
     */
    HBytes BytesNew(const Bytes *src, MSize start, MSize length) noexcept;

    HBytes BytesNew(Isolate *isolate,  OObject *object) noexcept;

    /**
     * @brief Initialize and create the specified type
     *
     * This function creates a new TypeInfo object representing the specific type.
     * It sets up the basic structure and core properties of the type.
     *
     * @param isolate Pointer to the Isolate in which the type is being created
     *
     * @return Handle to the newly created TypeInfo for the type, or an empty handle if creation failed
     */
    HOType BytesTypeInit(Isolate *isolate);

    /**
     * @brief Lexicographic byte-wise comparison.
     *
     * Returns a negative number if @p left precedes @p right, zero if
     * they are equal, a positive number otherwise. Mirrors `memcmp` and
     * extends it with length tie-breaking when the prefixes match.
     */
    int BytesCompare(const Bytes *left, const Bytes *right) noexcept;

    /**
     * @brief Searches for the first occurrence of a sequence of bytes (needle) in another sequence of bytes (haystack),
     * starting from the specified position.
     *
     * @param haystack The Bytes object that represents the larger sequence to search within.
     * @param needle The Bytes object that represents the sequence to search for.
     * @param start The starting position in the haystack from where the search will begin.
     * @return The zero-based index of the first occurrence of the needle within the haystack, or -1 if not found,
     *         or if the start position is invalid.
     */
    MSSize BytesFind(const Bytes *haystack, const Bytes *needle, MSize start) noexcept;
}

#endif // !ORBIT_ORBITER_DATATYPE_BYTES_H_
