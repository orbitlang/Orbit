// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_OBASE_H_
#define ORBIT_ORBITER_DATATYPE_OBASE_H_

#include <orbit/util/enum_bitmask.h>

#include <orbit/datatype.h>

#include <orbit/orbiter/memory/refcount.h>

namespace orbiter::datatype {
    using FunctionPtr = struct OObject *(*)(struct Function *, OObject **argv, OObject *kwargs, U16 argc);

    struct FunctionDef {
        /* Name of native function (this name will be exposed to Orbit) */
        const char *name;

        /* Documentation of native function (this doc will be exposed to Orbit) */
        const char *doc;

        /* Pointer to native code */
        FunctionPtr func;

        U16 params;

        /* Export as a method or a static function? */
        bool method;
    };

#define FUNCTIONDEF_SENTINEL {nullptr, nullptr, nullptr, 0, false}

    enum class PropertyDetail {
        INLINE = 0x01
    };

    struct PropertyDescriptor {
        /* Name of the property */
        const char *name;

        /* Length of the property name */
        U8 length;

        /* Pointer to the OObject representing the property's value */
        struct OObject *value;

        /* Additional details about the property */
        PropertyDetail detail;
    };

#define OROBJ_HEAD                                                      \
    struct {                                                            \
        orbiter::memory::RefCount ref_count_;                           \
        struct TypeInfo *type_;                                         \
    } head_

    enum class InstanceType {
        TYPE,

        ATOM,
        BYTES,
        DICT,
        DOUBLE,
        FUNCTION,
        LIST,
        MODULE,
        NAMESPACE,
        OBJECT,
        SET,
        STRING,
        TUPLE
    };
    constexpr const int kInstanceTypeCount = 13;

    struct TypeInfo {
        OROBJ_HEAD;

        /* Size of the object represented by this datatype (used for memory allocation) */
        U16 i_size;

        /* Additional headroom space */
        U8 headroom;

        /* Instance type (enum defining various object types in Orbit) */
        InstanceType i_type;

        struct {
            /* Array of property descriptors for this type */
            PropertyDescriptor *p_array;

            /* Number of properties in the array */
            U8 count;
        } properties;
    };

    struct OObject {
        OROBJ_HEAD;
    };

#define O_GET_HEAD(object)                 ((object)->head_)
#define O_GET_RC(object)                   (O_GET_HEAD(object).ref_count_)
#define O_UNSAFE_GET_RC(object)            (*((MSize *) &O_GET_HEAD(object).ref_count_))
#define O_GET_TYPE(object)                 (O_GET_HEAD(object).type_)

#define O_INCREF(object)                    (O_GET_RC(object).IncStrong(), object)
#define O_VFY_INCREF(object)                ((object != nullptr && !O_GET_RC(object).IncStrong()) ? nullptr : object)

#define O_GET_SLOT_COUNT(type)              (((type->i_size) - sizeof(OObject)) / sizeof(MSize))
}

ENUMBITMASK_ENABLE(orbiter::datatype::PropertyDetail);

#endif // !ORBIT_ORBITER_DATATYPE_OBASE_H_
