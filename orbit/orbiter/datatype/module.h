// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_MODULE_H_
#define ORBIT_ORBITER_DATATYPE_MODULE_H_

#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/code.h>

namespace orbiter::datatype {
    using ModuleNativeInitFn = const struct ModuleInit *(*)();

    using ModuleInitFn = bool (*)(struct Module *);
    using ModuleFiniFn = void (*)(Module *);

#define ORBIT_MODULE_INIT(__m_init)                                 \
    (extern "C" const struct ModuleInit *__orbit_module_init(void)  \
    { return (&__m_init); })

    constexpr auto kModuleInitFnName = "__orbit_module_init";

    struct ModuleEntry {
        const char *name;

        union {
            OObject *object;
            const FunctionDef *func;
        } prop;

        bool is_func;
    };

#define ORBIT_MODULE_EXPORT_FUNCTION(fn_native) \
{(fn_native).name, {.func=&(fn_native)}, true}

#define ORBIT_MODULE_EXPORT_TYPE(type)          \
{nullptr, {.object=(OObject *) (type)}, false}

#define ORBIT_MODULE_EXPORT_ALIAS(name, type)   \
{name, {.object=(OObject *) (type)}, false}

#define ORBIT_MODULE_SENTINEL {nullptr, nullptr, false}

    struct ModuleInit {
        const char *name;
        const char *doc;

        const char *version;

        const ModuleEntry *bulk;

        ModuleInitFn init;
        ModuleFiniFn fini;
    };

    struct Module {
        OROBJ_HEAD;
    };

    using HModule = Handle<Module>;

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
    bool ModuleTypeSetup(TypeInfo *self);

    /**
     * @brief Create a new module instance
     *
     * Constructs a new module object based on the provided type information.
     * This function initializes the module and returns a handle to the newly created module.
     *
     * @param tp_module Pointer to the TypeInfo that defines the type of the module
     *
     * @return Handle to the created module instance, or an empty handle if creation failed
     */
    HModule ModuleNew(TypeInfo *tp_module);

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
    HOType ModuleTypeInit(Isolate *isolate);

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
     * @return Handle to the newly created module TypeInfo, or an empty handle if creation failed
     */
    HOType ModuleTypeNew(Isolate *isolate, ORString *name, ORString *doc, U16 exported, U16 slots);

    HOType ModuleTypeNew(Isolate *isolate, const ModuleInit *init);

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
     * @return Handle to the newly created module TypeInfo, or an empty handle if creation failed
     */
    HOType ModuleTypeNew(const Code *code, ORString *name);
}

#endif // !ORBIT_ORBITER_DATATYPE_MODULE_H_
