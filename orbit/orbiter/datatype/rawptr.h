// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_RAWPTR_H_
#define ORBIT_ORBITER_DATATYPE_RAWPTR_H_

#include <atomic>

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    struct RawPtr {
        OROBJ_HEAD;

        std::atomic<PtrSize> ptr;
    };

    using HRawPtr = Handle<RawPtr>;

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
    bool RawPtrTypeSetup(TypeInfo *self);

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
    HOType RawPtrTypeInit(Isolate *isolate);

    /**
     * @brief Create a new raw pointer object in the given isolate
     *
     * This function initializes a new instance of a raw pointer object with the specified pointer value.
     *
     * @param isolate Pointer to Isolate where the raw pointer object is to be created
     * @param ptr Pointer value to associate with the newly created raw pointer object
     *
     * @return Handle to the created RawPtr object, or nullptr if creation fails
     */
    HRawPtr RawPtrNew(Isolate *isolate, void *ptr);

}

#endif // !ORBIT_ORBITER_DATATYPE_RAWPTR_H_
