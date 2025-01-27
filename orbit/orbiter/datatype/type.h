// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_TYPE_H_
#define ORBIT_ORBITER_DATATYPE_TYPE_H_

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    /**
     * @brief Initialize and create the specified type
     *
     * This function creates a new TypeInfo object representing the specific type.
     * It sets up the basic structure and core properties of the type.
     *
     * @param isolate Pointer to the Isolate in which the type is being created
     *
     * @return Pointer to the newly created TypeInfo for the type, or nullptr if creation failed
     */
    TypeInfo *TypeInit(Isolate *isolate);

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
    bool TypeSetup(TypeInfo *self);
}

#endif // !ORBIT_ORBITER_DATATYPE_TYPE_H_
