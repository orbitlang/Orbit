// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_BYTES_H_
#define ORBIT_ORBITER_DATATYPE_BYTES_H_

#include <orbit/orbiter/isolate.h>

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
    bool BytesTypeSetup(TypeInfo *self);

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
    HBytes BytesNew(Isolate *isolate, const unsigned char *buffer, MSize length, bool frozen);

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
    HBytes BytesNew(Isolate *isolate, MSize capacity, bool frozen);

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
    HBytes BytesNew(const Bytes *src, MSize start, MSize length);

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
}

#endif // !ORBIT_ORBITER_DATATYPE_BYTES_H_
