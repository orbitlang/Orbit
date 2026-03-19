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
     * @param self Pointer to TypeInfo created by %type%Init call
     *
     * @return true if setup was successful, false otherwise
     */
    bool NumberTypeSetup(TypeInfo *self);

    /**
     * @brief Creates a new Number object with an integer value.
     *
     * @param isolate Pointer to the Isolate in which the number is being created.
     * @param value The integer value to initialize the Number object with.
     * @return A handle to the newly created Number object.
     */
    HNumber IntNew(Isolate *isolate, IntegerUnderlying value);

    /**
     * @brief Creates a new Number object by parsing a string representation.
     *
     * @param isolate Pointer to the Isolate in which the number is being created.
     * @param string The string representation of the number.
     * @param base The numeric base (e.g., 10 for decimal, 16 for hexadecimal).
     * @return A handle to the newly created Number object.
     */
    HNumber IntNew(Isolate *isolate, const char *string, int base);

    /**
     * @brief Negate a signed integer stored in a small integer (SMI) representation
     *
     * This function negates the value held by a small integer (SMI) and returns it as a handle to a `Number` object.
     * If the SMI value is the minimum possible value, it creates a new `Number` object to handle the negation safely.
     *
     * @param isolate Pointer to the `Isolate` object, which provides instance-specific execution context.
     * @param value Pointer to an `OObject` containing the SMI value to be negated.
     *
     * @return A handle to a `Number` object containing the negated value. If the operation fails, an empty handle is returned.
     */
    HNumber SmiNeg(Isolate *isolate, OObject *value) noexcept;

    /**
     * @brief Creates a new Number object with an unsigned integer value.
     *
     * @param isolate Pointer to the Isolate in which the number is being created.
     * @param value The unsigned integer value to initialize the Number object with.
     * @return A handle to the newly created Number object.
     */
    HNumber UIntNew(Isolate *isolate, UIntegerUnderlying value);

    /**
    * @brief Creates a new unsigned Number object by parsing a string representation.
    *
    * @param isolate Pointer to the Isolate in which the number is being created.
    * @param string The string representation of the number.
    * @param base The numeric base (e.g., 10 for decimal, 16 for hexadecimal).
    * @return A handle to the newly created Number object.
    */
    HNumber UIntNew(Isolate *isolate, const char *string, int base);

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
    HOType NumberTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_NUMBER_H_
