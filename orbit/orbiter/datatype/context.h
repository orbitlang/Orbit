// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_CONTEXT_H
#define ORBIT_ORBITER_DATATYPE_CONTEXT_H

#include <orbit/orbiter/datatype/hashmap.h>
#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::datatype {
    class PropertyDetail {
    public:
        PropertyFlag flags;

        PropertyDetail() noexcept: flags() {
        }

        explicit PropertyDetail(const PropertyFlag flags) noexcept: flags(flags) {
        }

        PropertyDetail &operator=(const PropertyFlag flags) noexcept {
            this->flags = flags;
            return *this;
        }

        [[nodiscard]] bool IsConstant() const noexcept {
            return ENUMBITMASK_ISTRUE(this->flags, PropertyFlag::IS_CONSTANT);
        }

        [[nodiscard]] bool IsPublic() const noexcept {
            return ENUMBITMASK_ISTRUE(this->flags, PropertyFlag::IS_PUBLIC);
        }

        [[nodiscard]] bool IsWeak() const noexcept {
            return ENUMBITMASK_ISTRUE(this->flags, PropertyFlag::IS_WEAK);
        }
    };

    class PropertyStore {
    public:
        OObject *value;

        PropertyDetail detail;
    };

    using CtxHEntry = HEntry<ORString *, PropertyStore>;
    using CtxHMap = HashMap<
        ORString *,
        PropertyStore,
        ORStringEqual,
        ORStringHash
    >;

    struct Context {
        OROBJ_HEAD;

        CtxHMap names;
    };

    using HContext = Handle<Context>;

    /**
     * @brief Defines a property in the given context with the specified name, value, and flags
     *
     * This function allows creating or updating a property within the context. If the property
     * with the provided name already exists, its value and flags will be updated. If not, a new
     * property will be created and inserted into the context's property table.
     *
     * @param context Pointer to the context where the property will be defined
     * @param name Pointer to the string representing the name of the property
     * @param value Pointer to the object holding the value of the property
     * @param flags PropertyFlag containing metadata or attributes for the property
     *
     * @return true if the property was successfully created or updated, false otherwise
     */
    bool ContextDefine(Context *context, ORString *name, OObject *value, PropertyFlag flags);

    /**
     * @brief Defines a property in the given context with the specified name, value, and flags
     *
     * This function allows creating or updating a property within the context. If the property
     * with the provided name already exists, its value and flags will be updated. If not, a new
     * property will be created and inserted into the context's property table.
     *
     * @param context Pointer to the Context in which the property will be defined
     * @param name The name of the property to be defined
     * @param value Pointer to the OObject representing the property's value
     * @param flags PropertyFlag detailing additional attributes for the property
     *
     * @return true if the property was successfully defined, false otherwise
     */
    bool ContextDefine(Context *context, const char *name, OObject *value, PropertyFlag flags);

    /**
     * @brief Look up a property within a context by its name
     *
     * This function searches a specified context for a property entry corresponding to the given name.
     * If found, it retrieves the property's value and detailed information.
     *
     * @param context Pointer to the Context object in which the lookup is performed
     * @param name The name of the property to search for, provided as an ORString
     * @param out_value Reference to an HOObject that will contain the value of the found property, if successful
     * @param out_detail Pointer to a PropertyDetail structure that receives additional details of the property, if found
     *
     * @return true if the property was found and successfully retrieved, false otherwise
     */
    bool ContextLookup(const Context *context, ORString *name, HOObject &out_value, PropertyDetail *out_detail);

    /**
     * @brief Look up a property within a context by its name
     *
     * This function searches a specified context for a property entry corresponding to the given name.
     * If found, it retrieves the property's value and detailed information.
     *
     * @param context Pointer to the Context where the lookup will be performed
     * @param name The name of the property to look up
     * @param out_value Reference to an HOObject that will contain the value of the found property, if successful
     * @param out_detail Output parameter that will hold details about the found property
     *
     * @return true if the property is successfully found, false otherwise
     */
    bool ContextLookup(const Context *context, const char *name, HOObject &out_value, PropertyDetail *out_detail);

    /**
     * @brief Updates the value associated with a given name in the specified context.
     *
     * This function searches for the specified name within the provided context and updates
     * its associated value if the entry exists and is not marked as constant. If the name is not found
     * or the value is constant, the operation fails.
     *
     * @param context Pointer to the Context in which the value will be updated.
     * @param name Pointer to the ORString that represents the name of the entry to update.
     * @param value Pointer to the OObject containing the new value to set for the specified name.
     *
     * @return true if the value was successfully updated, false if the name was not found
     *         or the entry is marked as constant.
     */
    bool ContextSet(const Context *context, ORString *name, OObject *value);

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
    bool ContextSetup(TypeInfo *self);

    /**
     * @brief Creates a new context within the specified isolate
     *
     * This function allocates and initializes a new context object for the provided isolate.
     * It initializes the context's properties and ensures proper resource management.
     * The new context is returned wrapped in a handle for safe usage. If the context
     * creation or initialization fails, an empty handle is returned.
     *
     * @param isolate Pointer to the isolate in which the new context will be created
     *
     * @return HContext Handle to the newly created context, or an empty handle if creation fails
     */
    HContext ContextNew(Isolate *isolate);

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
    HOType ContextInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_CONTEXT_H
