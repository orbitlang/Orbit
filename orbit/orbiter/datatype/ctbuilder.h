// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_CTBUILDER_H_
#define ORBIT_ORBITER_DATATYPE_CTBUILDER_H_

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/code.h>

namespace orbiter::datatype {
    struct Class {
        OROBJ_HEAD;
    };

    using HClass = Handle<Class>;

    struct Trait {
        OROBJ_HEAD;
    };

    using HTrait = Handle<Trait>;

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
    HOType ClassTypeInit(Isolate *isolate);

    /**
     * @brief Creates a new class type with the specified properties and assigns traits.
     *
     * This function initializes a new TypeInfo object for a class type. It sets up the
     * class based on the provided `code`, inherits properties and structure from the
     * specified `super` class, and assigns traits if provided. Class properties are
     * added based on the exported symbols defined in the `code` object.
     *
     * @param code Pointer to the `Code` object containing data about the class, including
     * exported symbols and slot information.
     * @param super Pointer to the `TypeInfo` for the superclass. If null, the type is
     * created without any inheritance.
     * @param traits Array of pointers to `TypeInfo` objects representing traits to
     * be mixed into this class.
     * @param traits_count The number of elements in the `traits` array.
     *
     * @return A handle to the newly created TypeInfo object representing the class, or
     * an empty handle if the operation fails.
     */
    HOType ClassTypeNew(const Code *code, TypeInfo *super, TypeInfo **traits, U16 traits_count);

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
    HOType TraitTypeInit(Isolate *isolate);

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
    bool ClassTypeSetup(TypeInfo *self);

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
    bool TraitTypeSetup(TypeInfo *self);
}

#endif // !ORBIT_ORBITER_DATATYPE_CTBUILDER_H_
