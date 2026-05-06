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
}

#endif // !ORBIT_ORBITER_DATATYPE_BYTES_H_
