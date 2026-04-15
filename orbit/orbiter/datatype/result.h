// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_RESULT_H_
#define ORBIT_ORBITER_DATATYPE_RESULT_H_

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    struct Result {
        OROBJ_HEAD;

        bool ok;

        OObject *value;
    };

    using HResult = Handle<Result>;

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
    bool ResultTypeSetup(TypeInfo *self);

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
    HOType ResultTypeInit(Isolate *isolate);

    /**
     * @brief Creates a new Result object and initializes its properties
     *
     * This function creates an instance of the Result object, assigns the provided
     * OObject instance as its value, and sets the result status based on the given flag.
     *
     * @param isolate Pointer to the Isolate in which the Result object is created
     * @param object Pointer to the OObject to be assigned as the value of the Result
     * @param ok Boolean flag indicating the success status to be assigned to the Result
     *
     * @return A handle to the newly created Result object, or an empty handle if creation failed
     */
    HResult ResultNew(Isolate *isolate, OObject *object, bool ok);
}

#endif // !ORBIT_ORBITER_DATATYPE_RESULT_H_
