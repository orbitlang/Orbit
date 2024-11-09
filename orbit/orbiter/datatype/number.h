// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_NUMBER_H_
#define ORBIT_ORBITER_DATATYPE_NUMBER_H_

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    using IntegerUnderlying = I64;
    using UIntegerUnderlying = U64;

    struct Number {
        OROBJ_HEAD;

        union {
            IntegerUnderlying sint;
            UIntegerUnderlying uint;
        };
    };

    using HNumber = Handle<Number>;

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
     * @param isolate Pointer to the Isolate in which the type is being set up
     * @param self Pointer to TypeInfo created by %type%Init call
     *
     * @return true if setup was successful, false otherwise
     */
    bool NumberTypeSetup(Isolate *isolate, TypeInfo *self);

    /**
     * @brief Creates a new Number object with an integer value.
     *
     * @param isolate Pointer to the Isolate in which the number is being created.
     * @param value The integer value to initialize the Number object with.
     * @return A handle to the newly created Number object.
     */
    HNumber IntNew(const Isolate *isolate, IntegerUnderlying value);

    /**
     * @brief Creates a new Number object by parsing a string representation.
     *
     * @param isolate Pointer to the Isolate in which the number is being created.
     * @param string The string representation of the number.
     * @param base The numeric base (e.g., 10 for decimal, 16 for hexadecimal).
     * @return A handle to the newly created Number object.
     */
    HNumber IntNew(const Isolate *isolate, const char *string, int base);

    /**
     * @brief Creates a new Number object with an unsigned integer value.
     *
     * @param isolate Pointer to the Isolate in which the number is being created.
     * @param value The unsigned integer value to initialize the Number object with.
     * @return A handle to the newly created Number object.
     */
    HNumber UIntNew(const Isolate *isolate, UIntegerUnderlying value);

     /**
     * @brief Creates a new unsigned Number object by parsing a string representation.
     *
     * @param isolate Pointer to the Isolate in which the number is being created.
     * @param string The string representation of the number.
     * @param base The numeric base (e.g., 10 for decimal, 16 for hexadecimal).
     * @return A handle to the newly created Number object.
     */
    HNumber UIntNew(const Isolate *isolate, const char *string, int base);

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
    TypeInfo *NumberTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_NUMBER_H_
