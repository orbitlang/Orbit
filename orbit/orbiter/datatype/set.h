// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_SET_H_
#define ORBIT_ORBITER_DATATYPE_SET_H_

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/hashmap.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

namespace orbiter::datatype {
    struct Set {
        OROBJ_HEAD;

        sync::AsyncRWLock lock;

        ORHMap set;
    };

    using HSet = Handle<Set>;

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
    bool SetTypeSetup(TypeInfo *self);

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
    HOType SetTypeInit(Isolate *isolate);

    /**
     * @brief Creates a new Set instance with optional size initialization
     *
     * This function allocates and initializes a new Set object in the specified isolate.
     * If the allocation or initialization fails, the function cleans up and returns an empty handle.
     *
     * @param isolate The isolate in which the Set instance will be created
     * @param size Optional parameter specifying the initial size of the internal hash map. If the size is 0, a default initialization is performed.
     *
     * @return A handle to the newly created Set instance, or an empty handle if the creation fails
     */
    HSet SetNew(Isolate *isolate, U32 size);

    inline HSet SetNew(Isolate *isolate) {
        return SetNew(isolate, 0);
    }
}

#endif // !ORBIT_ORBITER_DATATYPE_SET_H_
