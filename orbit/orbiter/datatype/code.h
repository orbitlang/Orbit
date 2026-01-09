// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_CODE_H_
#define ORBIT_ORBITER_DATATYPE_CODE_H_

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/opcode.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/native/native.h>

namespace orbiter::datatype {
    struct ExportedSymbol {
        ORString *name;

        VariableFlags flags;

        U16 slot;
    };

    enum class NativeBindingType : U8 {
        FUNC,
        VAR,
        CONST
    };

    struct NativeParam {
        ORString *name;

        native::NativeType type;
    };

    struct NativeBinding {
        ORString *name;
        ORString *symbol;
        ORString *library;

        struct {
            NativeParam *params;
            U16 count;
        } params;

        native::NativeType ret_type;

        NativeBindingType binding_type;
    };

    struct Code {
        OROBJ_HEAD;

        const unsigned char *m_code;

        const unsigned char *m_end;

        struct {
            ExportedSymbol *symbols;

            U16 length;
        } exported;

        struct {
            NativeBinding *bindings;

            U16 length;
        } native;

        List *codes;

        List *unknown_symbols;

        List *static_resources;

        ORString *name;

        ORString *doc;

        union {
            U16 slots_count;
            U16 params_count;
        };

        U16 vars_count;

        U16 stack_size;

        MSize hash;

        void SetProps(ORString *name, ORString *doc) {
            this->name = O_INCREF(name);
            this->doc = O_INCREF(doc);
        }
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
    * @param self Pointer to TypeInfo created by %type%Init call
    *
    * @return true if setup was successful, false otherwise
    */
    bool CodeTypeSetup(TypeInfo *self);

    HCode CodeNew(Isolate *isolate, const unsigned char *m_code, List *unknown_symbols, List *static_resources,
                  U32 m_size, U16 slots_count, U16 stack_size);

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
    HOType CodeTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_CODE_H_
