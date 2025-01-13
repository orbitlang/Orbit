// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_CODE_H_
#define ORBIT_ORBITER_DATATYPE_CODE_H_

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::datatype {
    struct Code {
        OROBJ_HEAD;

        List *codes;

        List *names;

        List *static_resources;

        ORString *doc;

        const unsigned char *m_code;

        const unsigned char *m_end;

        U16 knames_length;

        U16 stack_size;

        MSize hash;
    };

    using HCode = Handle<Code>;

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
    bool CodeTypeSetup(Isolate *isolate, TypeInfo *self);

    HCode CodeNew(Isolate *isolate, List *codes, List *names, List *static_resources, ORString *doc,
                  const unsigned char *m_code, U32 m_size, U16 known_length, U16 stack_size);

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
    TypeInfo *CodeTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_CODE_H_
