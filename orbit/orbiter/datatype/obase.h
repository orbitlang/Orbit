// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_OBASE_H_
#define ORBIT_ORBITER_DATATYPE_OBASE_H_

#include <orbit/util/enum_bitmask.h>

#include <orbit/datatype.h>

#include <orbit/orbiter/memory/bitoffset.h>

#include <orbit/orbiter/sync/monitor.h>

namespace orbiter {
    class Isolate;
}

namespace orbiter::datatype {
    constexpr auto kOddBallMask = memory::PtrBitOffsets::SMITagMask | memory::PtrBitOffsets::OddBallMask;

    constexpr auto kOddBallNIL = nullptr;
    constexpr auto kOddBallFALSE = 0x08u | kOddBallMask;
    constexpr auto kOddBallTRUE = 0x10u | kOddBallMask;
    constexpr auto kOddBallSentinel = 0x21u | kOddBallMask;

#define BOOL_TO_OBOOL(value) ((value) ? kOddBallTRUE : kOddBallFALSE)
#define OBOOL_TO_BOOL(value) (((MSize)value) == kOddBallTRUE)

    // Orbit uses signed SMIs (Small Integers). The low two bits of a pointer-sized value are reserved for type tagging:
    //   bit 0 = SMI tag (1 = tagged immediate, 0 = heap pointer)
    //   bit 1 = oddball discriminator (1 = true/false/nil, 0 = SMI)
    //
    // This leaves (sizeof(MSize) * 8 - 2) bits for the SMI payload (including sign),
    // so on a 64-bit machine the effective range is [-(1<<61), (1<<61)-1].
    // Use O_TO_SMI / O_FROM_SMI rather than raw shifts to insulate callers from the tag layout.
    constexpr auto kSMIBit = (sizeof(MSize) * 8) - 2;
    constexpr auto kSMIMinSize = -(static_cast<MSSize>(1) << (kSMIBit - 1));
    constexpr auto kSMIMaxSize = (static_cast<MSSize>(1) << (kSMIBit - 1)) - 1;

    struct OObject;

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

    struct RefCount {
        static constexpr MSize kIsInstanceMask = ((uintptr_t) 1) << ((sizeof(void *) * 8) - 1);
        static constexpr MSize kCountMask = ~kIsInstanceMask;

        std::atomic_uintptr_t bits_{};

        explicit RefCount(const bool is_instance) noexcept : bits_(is_instance ? kIsInstanceMask : 0u) {
        }

        bool DecStrong() noexcept {
            return (bits_.fetch_sub(1, std::memory_order_release) & kCountMask) == 1;
        }

        bool IsInstance() const noexcept {
            return bits_.load(std::memory_order_relaxed) & kIsInstanceMask;
        }

        void IncStrong() noexcept {
            bits_.fetch_add(1, std::memory_order_relaxed);
        }

        MSize GetCount() const noexcept {
            return bits_.load(std::memory_order_relaxed) & kCountMask;
        }
    };

#define OROBJ_HEAD                                                      \
    struct {                                                            \
        RefCount ref_count_;                                            \
        struct TypeInfo *type_;                                         \
        std::atomic<orbiter::sync::Monitor *> mon_;                     \
    } head_

    enum class InstanceType : U8 {
        TYPE,

        ATOM,
        BOOLEAN,
        BYTES,
        CHANNEL,
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
        ITERATOR,
        LIST,
        MODULE,
        NAMESPACE,
        NIL,
        NATIVE_FUNC,
        NUMBER,
        OBJECT,
        RAWPTR,
        RESULT,
        SET,
        STRING,
        TRAIT,
        TUPLE
    };

    constexpr int kInstanceTypeCount = 29;

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

    enum class CallResult : I16 {
        ERROR = -1,
        BUSY = -2,
        CONTINUE = -3,
        DONE = -4,
        EXHAUST = -5
    };

    // --- Lifecycle ---
    using DtorFn = bool (*)(OObject *);
    using GCTraceCallback = void (*)(OObject *, MSize);
    using TraceFn = void (*)(const OObject *self, GCTraceCallback callback, MSize epoch);
    using TypeInfoAUXDtor = bool (*)(struct TypeInfo *self);

    // --- Comparison ---
    using CompareFn = int (*)(const OObject *, const OObject *);
    using ContainsFn = bool (*)(const OObject *, const OObject *, bool &);
    using EqualFn = bool (*)(const OObject *, const OObject *);

    // --- Arithmetic & Bitwise ---
    using BinaryFn = bool(*)(const OObject *, const OObject *, OObject *&result);
    using TernaryFn = bool(*)(const OObject *, const OObject *, OObject *);
    using UnaryFn = bool (*)(const OObject *, OObject *&result);

    // --- Slice ---
    using SliceLoadFn = bool (*)(const OObject *self, const OObject *start, const OObject *stop, const OObject *step,
                                 OObject *&result);

    using SliceStoreFn = bool (*)(const OObject *self, const OObject *start, const OObject *stop, const OObject *step,
                                  OObject *value);

    // --- Iteration ---
    using GetIterFn = OObject *(*)(OObject *);
    using IterNextFn = CallResult (*)(OObject *, OObject **);

    // --- Conversion ---
    using ToNativeType = bool (*)(OObject *, void *, NativeType);
    using ToBoolFn = bool (*)(const OObject *);
    using ToStrFn = OObject *(*)(Isolate *, const OObject *);

    // --- Runtime ---
    using HashFn = MSize (*)(const OObject *);

    // *****************************************************************************************************************
    // TYPE INFORMATION AND OPERATIONS
    // *****************************************************************************************************************

    struct TypeInfo {
        OROBJ_HEAD;

        const char *name;

        Isolate *isolate;

        DtorFn dtor;

        TraceFn trace;

        OObject *ctor;

        /// Trait Method Resolution Order
        OObject *mro;

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

        /// Size of the object represented by this datatype (used for memory allocation)
        U16 i_size;

        U16 offset;

        /// Additional headroom space
        U8 headroom;

        /// Instance type (enum defining various object types in Orbit)
        InstanceType i_type;
    };

    struct TypeOps {
        // --- Comparison ---
        CompareFn compare;
        ContainsFn contains;
        EqualFn equal;

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

        // --- Index ---
        BinaryFn load_index;
        TernaryFn store_index;

        // --- Slice ---
        SliceLoadFn load_slice;
        SliceStoreFn store_slice;

        // --- Unary ---
        UnaryFn neg;
        UnaryFn bit_not;

        // --- Iteration ---
        GetIterFn get_iter;
        GetIterFn get_riter;
        IterNextFn iter_next;

        // --- Conversion ---
        ToNativeType to_native;
        ToBoolFn to_bool;
        ToStrFn to_string;
        ToStrFn to_repr;

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
#define O_GET_MON(object)                   (O_GET_HEAD(object).mon_)
#define O_GET_TYPE(object)                  (O_GET_HEAD(object).type_)
#define O_GET_TYPE_OPS(object)              (((TypeInfoOps *)((unsigned char*) O_GET_TYPE(object)))->ops)
#define O_GET_ISOLATE(object)               (O_GET_TYPE(object)->isolate)

#define O_CAST(object, tp_info, type_)      ((type_ *) (((unsigned char*) object) + (tp_info)->offset))

#define O_SLOT(object, tp_info)             ((orbiter::datatype::OObject **) (O_CAST(object, tp_info, unsigned char) + (tp_info)->headroom))
#define O_SLOT_COUNT(object, tp_info)       (((tp_info)->i_size - (tp_info)->offset - (tp_info)->headroom) / sizeof(void *))

#define O_IS_SMI(object)                    ((((MSize)object) & orbiter::datatype::kOddBallMask) == 0x01)
#define O_IS_ODDBALL(object)                (((OObject *)object == nullptr) || ((!O_IS_SMI(object)) && (((MSize)object & orbiter::datatype::kOddBallMask) == orbiter::datatype::kOddBallMask)))
#define O_IS_OBJECT(object)                 ((object != nullptr) && ((!O_IS_SMI(object)) && (((MSize)object & orbiter::datatype::kOddBallMask) != orbiter::datatype::kOddBallMask)))

// Encode a signed integer into a tagged SMI.
// The caller is responsible for ensuring `num` fits in [kSMIMinSize, kSMIMaxSize];
// out-of-range values silently truncate the high bits.
#define O_TO_SMI(num)                       ((MSize)(((MSize)(MSSize)(num) << 2u) | 0x01u))

// Decode a tagged SMI back to its signed integer value. Arithmetic right shift preserves the sign.
// Callers must have already verified O_IS_SMI!
// This macro makes no attempt to distinguish SMIs from oddballs or heap pointers.
#define O_FROM_SMI(object)                  (((MSSize)(MSize)(object)) >> 2)

#define O_IS_FALSE(object)                  ((MSize)(object) == orbiter::datatype::kOddBallFALSE)
#define O_IS_TRUE(object)                   ((MSize)(object) == orbiter::datatype::kOddBallTRUE)
#define O_IS_NIL(object)                    (object == orbiter::datatype::kOddBallNIL)
#define O_IS_SENTINEL(object)               ((MSize)(object) == orbiter::datatype::kOddBallSentinel)
#define O_IS_TYPE(object, type)             (O_GET_TYPE(object)->i_type == type)

#define O_DECREF(object)                    (O_IS_OBJECT(object) ? (O_GET_RC(object).DecStrong(), object) : object)
#define O_FAST_DECREF(object)               ((object != nullptr) ? (O_GET_RC(object).DecStrong(), object) : object)

#define O_INCREF(object)                    (O_IS_OBJECT(object) ? (O_GET_RC(object).IncStrong(), object) : object)
#define O_FAST_INCREF(object)               ((object != nullptr) ? (O_GET_RC(object).IncStrong(), object) : object)

#define O_GET_SLOT_COUNT(type)              (((type->i_size) - sizeof(OObject)) / sizeof(MSize))
}

ENUMBITMASK_ENABLE(orbiter::datatype::PropertyFlag);

#endif // !ORBIT_ORBITER_DATATYPE_OBASE_H_
