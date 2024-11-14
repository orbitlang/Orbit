// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_OBASE_H_
#define ORBIT_ORBITER_DATATYPE_OBASE_H_

#include <orbit/util/enum_bitmask.h>

#include <orbit/datatype.h>

#include <orbit/orbiter/memory/refcount.h>

namespace orbiter {
    class Isolate;
}

namespace orbiter::datatype {
    constexpr auto kOddBallMask = memory::RCBitOffsets::InlineMask | memory::RCBitOffsets::StrongVFLAGMask;

    constexpr auto kOddBallNIL = nullptr;
    constexpr auto kOddBallFALSE = 0x08u | kOddBallMask;
    constexpr auto kOddBallTRUE = 0x10u | kOddBallMask;

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
        struct ORString *name; // from: orstring.h

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
        CODE,
        DECIMAL,
        DICT,
        FUNCTION,
        LIST,
        MODULE,
        NAMESPACE,
        NUMBER,
        OBJECT,
        SET,
        STRING,
        TUPLE
    };

    constexpr int kInstanceTypeCount = 15;

    using TypeInfoAUXDtor = bool (*)(struct TypeInfo *self);

    struct TypeInfo {
        OROBJ_HEAD;

        /* Size of the object represented by this datatype (used for memory allocation) */
        U16 i_size;

        /* Additional headroom space */
        U8 headroom;

        /* Instance type (enum defining various object types in Orbit) */
        InstanceType i_type;

        Isolate *isolate;

        /* Auxiliary data storage for type-specific information */
        struct {
            /* Pointer to type-specific auxiliary data.
             * The structure and content of this data depends on the specific type */
            void *data;

            /* Function pointer to auxiliary data destructor.
             * Called when the type is destroyed to cleanup any auxiliary data */
            TypeInfoAUXDtor dtor;
        } aux;

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

#define O_IS_SMI(object)                    ((MSize)object & 0x01u)
#define O_IS_ODDBALL(object)                ((!O_IS_SMI(object)) && (((MSize)object & kOddBallMask) == kOddBallMask))
#define O_IS_OBJECT(object)                 ((!O_IS_SMI(object)) && (((MSize)object & kOddBallMask) != kOddBallMask))

#define O_IS_FALSE(object)                  (O_IS_ODDBALL(object) && ((object & kOddBallFALSE) == kOddBallFALSE))
#define O_IS_TRUE(object)                   (O_IS_ODDBALL(object) && ((object & kOddBallTRUE) == kOddBallTRUE))
#define O_IS_NIL(object)                    (object == kOddBallNIL)

#define O_INCREF(object)                    (O_IS_OBJECT(object) && O_GET_RC(object).IncStrong(), object)
#define O_VFY_INCREF(object)                ((object != nullptr && (O_IS_OBJECT(object) && O_GET_RC(object).IncStrong())) ? nullptr : object)

#define O_GET_SLOT_COUNT(type)              (((type->i_size) - sizeof(OObject)) / sizeof(MSize))
}

ENUMBITMASK_ENABLE(orbiter::datatype::PropertyDetail);

#endif // !ORBIT_ORBITER_DATATYPE_OBASE_H_
