// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_DECIMAL_H_
#define ORBIT_ORBITER_DATATYPE_DECIMAL_H_

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    using DecimalUnderlying = long double;

    struct Decimal {
        OROBJ_HEAD;

        DecimalUnderlying value;
    };

    using HDecimal = Handle<Decimal>;

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
    bool DecimalTypeSetup(TypeInfo *self);

    /**
     * @brief Creates a new Decimal object and initializes it with the specified value
     *
     * This function creates a new Decimal instance within the given isolate and assigns
     * the provided numeric value to it.
     *
     * @param isolate The isolate in which the Decimal object will be created
     * @param number The initial value to assign to the newly created Decimal object
     *
     * @return A handle to the newly created Decimal object, or an empty handle if creation fails
     */
    HDecimal DecimalNew(Isolate *isolate, DecimalUnderlying number);

    /**
     * @brief Creates a new Decimal object and initializes it with a string representation of its value
     *
     * This function constructs a new Decimal object within the given isolate and parses the provided string
     * to initialize its value.
     *
     * @param isolate The isolate in which the Decimal object will be created
     * @param string Null-terminated string representing the decimal value to be set
     *
     * @return Handle to the newly created Decimal object if successful, or an empty handle if the object creation fails
     */
    HDecimal DecimalNew(Isolate *isolate, const char *string);

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
    HOType DecimalTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_DECIMAL_H_
