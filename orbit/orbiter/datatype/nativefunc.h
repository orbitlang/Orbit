// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_NATIVEFUNC_H_
#define ORBIT_ORBITER_DATATYPE_NATIVEFUNC_H_

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/native/dlwrap.h>
#include <orbit/orbiter/native/ffi.h>

namespace orbiter::datatype {
    struct NativeFunc {
        OROBJ_HEAD;

        ORString *name;

        ORString *doc;

        native::DLHandle handle;

        native::NativeParam *params;

        U16 arity;

        NativeType ret_type;
    };

    using HNativeFunc = Handle<NativeFunc>;

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
    bool NativeFuncTypeSetup(TypeInfo *self);

    /**
     * @brief Creates a new native function object
     *
     * This function initializes a native function object.
     * It sets up the function's name, return type, parameter types, and argument list based on the provided binding information.
     *
     * @param isolate Pointer to the active Isolate
     * @param binding Pointer to the NativeBinding containing metadata about the function
     * @param handle Dynamic library handle associated with the native function
     *
     * @return A handle to the newly created NativeFunc object, or an empty handle if the creation fails
     */
    HNativeFunc NativeFuncNew(Isolate *isolate, native::NativeBinding *binding, native::DLHandle handle);

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
    HOType NativeFuncTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_NATIVEFUNC_H_
