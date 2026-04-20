// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/datatype/rawptr.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Two raw pointers are equal when they hold the same address.
static bool RawPtrEqual(const OObject *left, const OObject *right) {
    if (left == right)
        return true;

    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::RAWPTR))
        return false;

    return ((const RawPtr *) left)->ptr.load(std::memory_order_relaxed) ==
           ((const RawPtr *) right)->ptr.load(std::memory_order_relaxed);
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A non-null raw pointer is truthy.
static bool RawPtrToBool(const OObject *self) {
    return ((const RawPtr *) self)->ptr.load(std::memory_order_relaxed) != 0;
}

static OObject *RawPtrToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto *rawptr = (const RawPtr *) self;
    const auto s = ORStringFormat(isolate, "Rawptr(%p)", rawptr->ptr.load(std::memory_order_relaxed));

    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// TYPE OPS — RUNTIME
// *********************************************************************************************************************

/// Hash the pointer address; null maps to 1 to avoid the zero sentinel.
static MSize RawPtrHash(const OObject *self) {
    const auto h = ((const RawPtr *) self)->ptr.load(std::memory_order_relaxed);

    return h != 0 ? (MSize) h : 1;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_METHOD(rawptr_address, address,
               R"DOC(
@brief Return the numeric address of this pointer.

@return The pointer value as an unsigned Int.

@see is_null
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];

    auto n = UIntNew(O_GET_ISOLATE(self), self->ptr.load(std::memory_order_relaxed));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(rawptr_add, add,
               R"DOC(
@brief Advance this pointer in-place by `bytes`.

Supports negative offsets to move backwards.

@param bytes  Signed byte count to add to the address.

@return nil

@see sub, offset
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("bytes", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *self = (RawPtr *) argv[0];

    IntegerUnderlying bytes;
    NumberExtract(argv[1], bytes);

    self->ptr.fetch_add((PtrSize) (MSSize) bytes, std::memory_order_relaxed);

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_free, free,
               R"DOC(
@brief Release the memory at this pointer's address by calling free().

The pointer is zeroed after the call to prevent use-after-free.
Has no effect if the pointer is already null.

@see address, is_null
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    auto *self = (RawPtr *) argv[0];

    const auto addr = self->ptr.exchange(0, std::memory_order_relaxed);

    if (addr != 0)
        ::free((void *) addr);

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_is_null, is_null,
               R"DOC(
@brief Return true if this pointer is null.

@return true if the address is zero, false otherwise.

@see address
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];

    return HOObject((OObject *) BOOL_TO_OBOOL(self->ptr.load(std::memory_order_relaxed) == 0));
}

RUNTIME_METHOD(rawptr_offset, offset,
               R"DOC(
@brief Return a new RawPtr advanced by `bytes` from this pointer.

Supports negative offsets to move backwards.

@param bytes  Signed byte count to add to the address.

@return A new RawPtr pointing to `self + bytes`.

@panic TypeError  When `bytes` is not an integer.

@see add, sub, address, read_ptr
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("bytes", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    IntegerUnderlying bytes;
    NumberExtract(argv[1], bytes);

    const auto addr = self->ptr.load(std::memory_order_relaxed);
    auto p = RawPtrNew(isolate, (void *) (addr + bytes));
    if (!p)
        return {};

    return HOObject(std::move(p));
}

RUNTIME_METHOD(rawptr_read_bytes, read_bytes,
               R"DOC(
@brief Read `size` bytes from the pointed-to address and return them as a Bytes object.

@param size  Number of bytes to read.

@return A Bytes object containing the copied data.

@panic RuntimeError  When the pointer is null.

@see read_i8, read_i64
)DOC", 2, nullptr, false, false) {
    // TODO: implement once Bytes type is available
    assert(false);
}

RUNTIME_METHOD(rawptr_read_f64, read_f64,
               R"DOC(
@brief Read a 64-bit floating-point value from the pointed-to address.

@return The value as a Decimal.

@panic RuntimeError  When the pointer is null.

@see read_i64, write_f64
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    DecimalUnderlying val;

    memcpy(&val, (void *) addr, sizeof(val));

    auto n = DecimalNew(isolate, val);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(rawptr_read_i8, read_i8,
               R"DOC(
@brief Read a signed 8-bit integer from the pointed-to address.

@return The value as an Int.

@panic RuntimeError  When the pointer is null.

@see read_i16, read_i32, read_i64, read_u8
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    int8_t val;

    memcpy(&val, (void *) addr, sizeof(val));

    return HOObject((OObject *) O_TO_SMI(val));
}

RUNTIME_METHOD(rawptr_read_i16, read_i16,
               R"DOC(
@brief Read a signed 16-bit integer from the pointed-to address.

@return The value as an Int.

@panic RuntimeError  When the pointer is null.

@see read_i8, read_i32, read_i64, read_u16
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    I16 val;

    memcpy(&val, (void *) addr, sizeof(val));

    return HOObject((OObject *) O_TO_SMI(val));
}

RUNTIME_METHOD(rawptr_read_i32, read_i32,
               R"DOC(
@brief Read a signed 32-bit integer from the pointed-to address.

@return The value as an Int.

@panic RuntimeError  When the pointer is null.

@see read_i8, read_i16, read_i64, read_u32
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    I32 val;

    memcpy(&val, (void *) addr, sizeof(val));

    auto n = IntNew(isolate, val);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(rawptr_read_i64, read_i64,
               R"DOC(
@brief Read a signed 64-bit integer from the pointed-to address.

@return The value as an Int.

@panic RuntimeError  When the pointer is null.

@see read_i8, read_i16, read_i32, read_u64
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    I64 val;

    memcpy(&val, (void *) addr, sizeof(val));

    auto n = IntNew(isolate, val);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(rawptr_read_ptr, read_ptr,
               R"DOC(
@brief Read a pointer-sized value from the pointed-to address and wrap it in a RawPtr.

@return A new RawPtr holding the value at self's address.

@panic RuntimeError  When the pointer is null.

@see offset, address
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    void *val;

    memcpy(&val, (void *) addr, sizeof(val));

    auto p = RawPtrNew(isolate, val);
    if (!p)
        return {};

    return HOObject(std::move(p));
}

RUNTIME_METHOD(rawptr_read_u8, read_u8,
               R"DOC(
@brief Read an unsigned 8-bit integer from the pointed-to address.

@return The value as an Int.

@panic RuntimeError  When the pointer is null.

@see read_u16, read_u32, read_u64, read_i8
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    uint8_t val;

    memcpy(&val, (void *) addr, sizeof(val));

    return HOObject((OObject *) O_TO_SMI(val));
}

RUNTIME_METHOD(rawptr_read_u16, read_u16,
               R"DOC(
@brief Read an unsigned 16-bit integer from the pointed-to address.

@return The value as an Int.

@panic RuntimeError  When the pointer is null.

@see read_u8, read_u32, read_u64, read_i16
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    uint16_t val;

    memcpy(&val, (void *) addr, sizeof(val));

    return HOObject((OObject *) O_TO_SMI(val));
}

RUNTIME_METHOD(rawptr_read_u32, read_u32,
               R"DOC(
@brief Read an unsigned 32-bit integer from the pointed-to address.

@return The value as an unsigned Int.

@panic RuntimeError  When the pointer is null.

@see read_u8, read_u16, read_u64, read_i32
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    uint32_t val;

    memcpy(&val, (void *) addr, sizeof(val));

    auto n = UIntNew(isolate, val);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(rawptr_read_u64, read_u64,
               R"DOC(
@brief Read an unsigned 64-bit integer from the pointed-to address.

@return The value as an unsigned Int.

@panic RuntimeError  When the pointer is null.

@see read_u8, read_u16, read_u32, read_i64
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RAWPTR));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    uint64_t val;

    memcpy(&val, (void *) addr, sizeof(val));

    auto n = UIntNew(isolate, val);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(rawptr_sub, sub,
               R"DOC(
@brief Retreat this pointer in-place by `bytes`.

Supports negative offsets to move forwards.

@param bytes  Signed byte count to subtract from the address.

@return nil

@see add, offset
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("bytes", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *self = (RawPtr *) argv[0];

    IntegerUnderlying bytes;
    NumberExtract(argv[1], bytes);

    self->ptr.fetch_sub((PtrSize) (MSSize) bytes, std::memory_order_relaxed);

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_f64, write_f64,
               R"DOC(
@brief Write a 64-bit floating-point value to the pointed-to address.

@param value  The decimal value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not a Decimal.

@see write_i32, read_f64
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    const auto val = (DecimalUnderlying) ((const Decimal *) argv[1])->value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_i8, write_i8,
               R"DOC(
@brief Write a signed 8-bit integer to the pointed-to address.

Only the lower 8 bits of `value` are written.

@param value  The integer value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not an integer.

@see write_i16, write_i32, write_i64, write_u8
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (int8_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_i16, write_i16,
               R"DOC(
@brief Write a signed 16-bit integer to the pointed-to address.

Only the lower 16 bits of `value` are written.

@param value  The integer value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not an integer.

@see write_i8, write_i32, write_i64, write_u16
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (int16_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_i32, write_i32,
               R"DOC(
@brief Write a signed 32-bit integer to the pointed-to address.

Only the lower 32 bits of `value` are written.

@param value  The integer value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not an integer.

@see write_f64, write_i8, write_i16, write_i64, read_i32
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (int32_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_i64, write_i64,
               R"DOC(
@brief Write a signed 64-bit integer to the pointed-to address.

@param value  The integer value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not an integer.

@see write_i8, write_i16, write_i32, write_u64
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (int64_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_u8, write_u8,
               R"DOC(
@brief Write an unsigned 8-bit integer to the pointed-to address.

Only the lower 8 bits of `value` are written.

@param value  The integer value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not an integer.

@see write_u16, write_u32, write_u64, write_i8
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (uint8_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_u16, write_u16,
               R"DOC(
@brief Write an unsigned 16-bit integer to the pointed-to address.

Only the lower 16 bits of `value` are written.

@param value  The integer value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not an integer.

@see write_u8, write_u32, write_u64, write_i16
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (uint16_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_u32, write_u32,
               R"DOC(
@brief Write an unsigned 32-bit integer to the pointed-to address.

Only the lower 32 bits of `value` are written.

@param value  The integer value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not an integer.

@see write_u8, write_u16, write_u64, write_i32
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (uint32_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(rawptr_write_u64, write_u64,
               R"DOC(
@brief Write an unsigned 64-bit integer to the pointed-to address.

@param value  The integer value to write.

@panic RuntimeError  When the pointer is null.
@panic TypeError     When `value` is not an integer.

@see write_u8, write_u16, write_u32, write_i64
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("value", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (addr == 0) {
        ErrorSet(O_GET_ISOLATE(self),
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 "null pointer dereference");

        return {};
    }

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (uint64_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

constexpr FunctionDef rawptr_methods[] = {
    rawptr_address,
    rawptr_add,
    rawptr_free,
    rawptr_is_null,
    rawptr_offset,
    rawptr_read_bytes,
    rawptr_read_f64,
    rawptr_read_i8,
    rawptr_read_i16,
    rawptr_read_i32,
    rawptr_read_i64,
    rawptr_read_ptr,
    rawptr_read_u8,
    rawptr_read_u16,
    rawptr_read_u32,
    rawptr_read_u64,
    rawptr_sub,
    rawptr_write_f64,
    rawptr_write_i8,
    rawptr_write_i16,
    rawptr_write_i32,
    rawptr_write_i64,
    rawptr_write_u8,
    rawptr_write_u16,
    rawptr_write_u32,
    rawptr_write_u64,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::RawPtrTypeSetup(TypeInfo *self) {
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = RawPtrEqual;
    ops.to_bool = RawPtrToBool;
    ops.to_string = RawPtrToString;
    ops.hash = RawPtrHash;

    return TIPropertyAdd(self, rawptr_methods, PropertyFlag::IS_PUBLIC);
}

HOType orbiter::datatype::RawPtrTypeInit(Isolate *isolate) {
    auto rawptr = MakeType(isolate, "Rawptr", InstanceType::RAWPTR, sizeof(RawPtr) - sizeof(OObject), 26, 0);
    return rawptr;
}

HRawPtr orbiter::datatype::RawPtrNew(Isolate *isolate, void *ptr) {
    auto *rawptr = MakeObject<RawPtr>(isolate, InstanceType::RAWPTR);
    if (rawptr != nullptr)
        rawptr->ptr.store((PtrSize) ptr, std::memory_order_relaxed);

    O_GC_TRACK_RETURN(isolate, rawptr, false);
}
