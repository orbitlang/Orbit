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
    constexpr auto kOddBallMask = (MSize) memory::RCBitOffsets::InlineMask | memory::RCBitOffsets::StrongVFLAGMask;

    constexpr auto kSMIBit = (sizeof(MSize) * 8) - 1;

    // Orbit uses signed SMI (Small Integers) by default
    constexpr auto kSMIMinSize = -(static_cast<MSSize>(1) << (kSMIBit - 1));
    constexpr auto kSMIMaxSize = (static_cast<MSSize>(1) << (kSMIBit - 1)) - 1;

    constexpr auto kOddBallNIL = nullptr;
    constexpr auto kOddBallFALSE = 0x08u | kOddBallMask;
    constexpr auto kOddBallTRUE = 0x10u | kOddBallMask;

#define BOOL_TO_OBOOL(value) ((value) ? kOddBallTRUE : kOddBallFALSE)
#define OBOOL_TO_BOOL(value) ((value) == kOddBallTRUE)

    enum class PropertyFlag:U8 {
        IN_OBJECT = 0x01,

        IS_CONSTANT = 0x01 << 1,
        IS_WEAK = 0x01 << 2,
        IS_PROTECTED = 0x01 << 3,
        IS_PUBLIC = 0x01 << 4,

        DUP_INLINE = 0x01 << 5
    };

    struct PropertyDescriptor {
        struct ORString *name; // from: orstring.h

        /// Pointer to the OObject representing the property's value
        OObject *value;

        /// Represents the index for a specific property within an object
        U16 slot;

        /// Additional details about the property
        PropertyFlag detail;
    };

#define OPROPERTY_ENTRY(name, offset, details)  {name, offset, details}
#define OPROPERTY_SENTINEL                      {nullptr, 0, PropertyFlag::IN_OBJECT}

    struct OPropertyEntry {
        const char *name;

        /// Represents the index for a specific property within an object
        U16 slot;

        /// Additional details about the property
        PropertyFlag details;
    };

#define OROBJ_HEAD                                                      \
    struct {                                                            \
        orbiter::memory::RefCount ref_count_;                           \
        struct TypeInfo *type_;                                         \
        bool is_instance;                                               \
    } head_

    enum class InstanceType {
        TYPE,

        ATOM,
        BOOLEAN,
        BYTES,
        CLASS,
        CLOSURE,
        CODE,
        CONTEXT,
        DECIMAL,
        DICT,
        ERROR,
        FUNCTION,
        FUTURE,
        GENERATOR,
        LIST,
        MODULE,
        NAMESPACE,
        NIL,
        NATIVE_FUNC,
        NUMBER,
        OBJECT,
        RAWPTR,
        RESULT,
        // SET,
        STRING,
        TRAIT,
        TUPLE
    };

    constexpr const char *InstanceTypeNames[] = {
        "Type",
        "Atom",
        "Boolean",
        "Bytes",
        "Class",
        "Closure",
        "Code",
        "Context",
        "Decimal",
        "Dict",
        "Error",
        "Function",
        "Future",
        "Generator",
        "List",
        "Module",
        "Namespace",
        "Nil",
        "NativeFunc",
        "Number",
        "Object",
        "RawPtr",
        "Result",
        //"Set",
        "String",
        "Trait",
        "Tuple"
    };

    constexpr int kInstanceTypeCount = 26;

    enum class NativeType {
        BOOL,
        BYTE,

        I8,
        I16,
        I32,
        I64,
        ISIZE,

        U8,
        U16,
        U32,
        U64,
        USIZE,
        UNIT,
        PTR,

        F32,
        F64
    };

    constexpr const char *NativeTypeNames[] = {
        "bool", // BOOL
        "uint8_t", // BYTE (unsigned char)

        "int8_t", // I8
        "int16_t", // I16
        "int32_t", // I32
        "int64_t", // I64
        "intptr_t", // ISIZE (ssize_t)

        "uint8_t", // U8
        "uint16_t", // U16
        "uint32_t", // U32
        "uint64_t", // U64
        "size_t", // USIZE

        "void", // UNIT
        "void*", // PTR

        "float", // F32
        "double" // F64
    };

    // *****************************************************************************************************************
    // HELPER FUNCTIONS
    // *****************************************************************************************************************

    // --- Lifecycle ---
    using DtorFn          = bool (*)(OObject *);
    using GCTraceCallback = void (*)(OObject *, MSize);
    using TraceFn         = void (*)(const OObject *self, GCTraceCallback callback, MSize epoch);
    using TypeInfoAUXDtor = bool (*)(struct TypeInfo *self);

    // --- Comparison ---
    using CompareFn = int (*)(const OObject *, const OObject *);
    using EqualFn   = bool (*)(const OObject *, const OObject *);

    // --- Arithmetic & Bitwise ---
    using BinaryFn = bool(*)(const OObject *, const OObject *, OObject *&result);
    using UnaryFn  = bool (*)(const OObject *, OObject *&result);

    // --- Iteration ---
    using GetIterFn  = OObject *(*)(OObject *);
    using IterNextFn = bool (*)(OObject *, OObject **);

    // --- Conversion ---
    using ToNativeType = bool (*)(OObject *, void *, NativeType);
    using ToBoolFn     = bool (*)(const OObject *);
    using ToStrFn      = OObject *(*)(Isolate *, const OObject *);

    // --- Runtime ---
    using HashFn = MSize (*)(const OObject *);

    // *****************************************************************************************************************
    // TYPE INFORMATION AND OPERATIONS
    // *****************************************************************************************************************

    struct TypeInfo {
        OROBJ_HEAD;

        /// Size of the object represented by this datatype (used for memory allocation)
        U16 i_size;

        U16 offset;

        /// Additional headroom space
        U8 headroom;

        /// Instance type (enum defining various object types in Orbit)
        InstanceType i_type;

        /// Trait Method Resolution Order
        OObject *mro;

        Isolate *isolate;

        DtorFn dtor;

        TraceFn trace;

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

    struct TypeOps {
        // --- Comparison ---
        CompareFn compare;
        EqualFn   equal;

        // --- Arithmetic ---
        BinaryFn add;
        BinaryFn sub;
        BinaryFn mul;
        BinaryFn div;
        BinaryFn idiv;
        BinaryFn mod;

        // --- Bitwise ---
        BinaryFn bit_and;
        BinaryFn bit_or;
        BinaryFn bit_xor;
        BinaryFn lshift;
        BinaryFn rshift;

        // --- Unary ---
        UnaryFn neg;
        UnaryFn bit_not;

        // --- Iteration ---
        GetIterFn  get_iter;
        IterNextFn iter_next;

        // --- Conversion ---
        ToNativeType to_native;
        ToBoolFn     to_bool;
        ToStrFn      to_string;
        ToStrFn      to_repr;

        // --- Runtime ---
        HashFn hash;
    };

    struct TypeInfoOps {
        TypeInfo type;
        TypeOps ops;
    };

    struct OObject {
        OROBJ_HEAD;
    };

    // *****************************************************************************************************************
    // HELPER MACROS
    // *****************************************************************************************************************

#define O_GET_HEAD(object)                  ((object)->head_)
#define O_GET_RC(object)                    (O_GET_HEAD(object).ref_count_)
#define O_UNSAFE_GET_RC(object)             (*((MSize *) &O_GET_HEAD(object).ref_count_))
#define O_GET_TYPE(object)                  (O_GET_HEAD(object).type_)
#define O_GET_TYPE_OPS(object)              (((TypeInfoOps *)((unsigned char*) O_GET_TYPE(object)))->ops)
#define O_GET_ISOLATE(object)               (O_GET_TYPE(object)->isolate)

#define O_CAST(object, tp_info, type_)      ((type_ *) (((unsigned char*) object) + (tp_info)->offset))

#define O_SLOT(object, tp_info)             ((orbiter::datatype::OObject **) (O_CAST(object, tp_info, unsigned char) + (tp_info)->headroom))
#define O_SLOT_COUNT(object, tp_info)       (((tp_info)->i_size - (tp_info)->offset - (tp_info)->headroom) / sizeof(void *))

#define O_IS_SMI(object)                    (((MSize)object & 0x01u) != 0)
#define O_IS_ODDBALL(object)                ((object == nullptr) || ((!O_IS_SMI(object)) && (((MSize)object & orbiter::datatype::kOddBallMask) == orbiter::datatype::kOddBallMask)))
#define O_IS_OBJECT(object)                 ((object != nullptr) && ((!O_IS_SMI(object)) && (((MSize)object & orbiter::datatype::kOddBallMask) != orbiter::datatype::kOddBallMask)))
#define O_TO_SMI(num)                       (((num) << 1) | 0x01)

#define O_IS_FALSE(object)                  (O_IS_ODDBALL(object) && ((object & orbiter::datatype::kOddBallFALSE) == orbiter::datatype::kOddBallFALSE))
#define O_IS_TRUE(object)                   (O_IS_ODDBALL(object) && ((object & orbiter::datatype::kOddBallTRUE) == orbiter::datatype::kOddBallTRUE))
#define O_IS_NIL(object)                    (object == orbiter::datatype::kOddBallNIL)
#define O_IS_TYPE(object, type)             (O_GET_TYPE(object)->i_type == type)

#define O_DECREF(object)                    (O_IS_OBJECT(object) ? (O_GET_RC(object).DecStrong(nullptr), object) : object)
#define O_FAST_DECREF(object)               ((object != nullptr) ? (O_GET_RC(object).DecStrong(nullptr), object) : object)

#define O_INCREF(object)                    (O_IS_OBJECT(object) ? (O_GET_RC(object).IncStrong(), object) : object)
#define O_FAST_INCREF(object)               ((object != nullptr) ? (O_GET_RC(object).IncStrong(), object) : object)

#define O_GET_SLOT_COUNT(type)              (((type->i_size) - sizeof(OObject)) / sizeof(MSize))
}

ENUMBITMASK_ENABLE(orbiter::datatype::PropertyFlag);

#endif // !ORBIT_ORBITER_DATATYPE_OBASE_H_
