// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_ATOM_H_
#define ORBIT_ORBITER_DATATYPE_ATOM_H_

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::datatype {
    struct Atom {
        OROBJ_HEAD;

        ORString *id;
    };

    using HAtom = Handle<Atom>;

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
    bool AtomTypeSetup(TypeInfo *self);

    /**
     * @brief Create new atom
     *
     * @param isolate Pointer to the Isolate
     * @param string The C-string to convert to Orbit atom
     * @param length The length of the C-string
     *
     * @return Handle to Atom object
     */
    HAtom AtomNew(Isolate *isolate, const char *string, MSize length);

    /**
     * @brief Create new atom
     *
     * @param isolate Pointer to the Isolate
     * @param string The null-terminated C-string to convert to Orbit atom
     *
     * @return Handle to Atom object
     */
    inline HAtom AtomNew(Isolate *isolate, const char *string) {
        return AtomNew(isolate, string, strlen(string));
    }

    /**
     * @brief Create new atom from Orbit string
     *
     * @param isolate Pointer to the Isolate
     * @param id The ORString to convert to atom
     *
     * @return Handle to Atom object
     */
    HAtom AtomNew(Isolate *isolate, ORString *id);

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
    TypeInfo *AtomTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_ATOM_H_
