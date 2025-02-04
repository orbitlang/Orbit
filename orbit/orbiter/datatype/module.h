// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_MODULE_H_
#define ORBIT_ORBITER_DATATYPE_MODULE_H_

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/code.h>

namespace orbiter::datatype {
    struct Module {
        OROBJ_HEAD;
    };

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
    TypeInfo *ModuleInit(Isolate *isolate);

    /**
     * @brief Create a new module type with specified properties and slots
     *
     * This function initializes a new module TypeInfo object in the given Isolate.
     * It sets up the module with provided name, documentation string, exported properties,
     * and slot count. Constant properties for name and documentation are also added.
     *
     * @param isolate Pointer to the Isolate where the module is being created
     * @param name Pointer to an ORString representing the name of the module
     * @param doc Pointer to an ORString containing the documentation string for the module
     * @param exported Number of exported properties the module contains
     * @param slots Number of additional slots allocated for the module
     *
     * @return Pointer to the newly created module TypeInfo, or nullptr if creation failed
     */
    TypeInfo *ModuleNew(Isolate *isolate, ORString *name, ORString *doc, U16 exported, U16 slots);

    /**
     * @brief Creates a new module with the given code and name
     *
     * This function constructs a new module represented by a TypeInfo object.
     * It establishes its properties, adds exported symbols, and sets up slot mappings.
     * If the provided code contains exported symbols, they are added as properties
     * to the created module.
     *
     * @param code Pointer to the Code object which defines the structure and symbols for the module
     * @param name Pointer to the ORString object that specifies the name of the module
     *
     * @return Pointer to the newly created TypeInfo object for the module, or nullptr if creation failed
     */
    TypeInfo *ModuleNew(Code *code, ORString *name);

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
    bool ModuleSetup(TypeInfo *self);
}

#endif // !ORBIT_ORBITER_DATATYPE_MODULE_H_
