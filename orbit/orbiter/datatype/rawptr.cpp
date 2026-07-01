// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0


#include <orbit/orbiter/datatype/bytes.h>
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

static bool RawPtrToNative(const RawPtr *self, void *out, const NativeType type) {
    switch (type) {
        // A raw pointer is an address: marshal it either as a pointer or as a
        // pointer-width integer (the `uintptr_t` / `intptr_t` / `size_t` shape),
        // so it can be passed to a native func that types the address as
        // `ptr`, `uSize` or `iSize`. Narrower integer types are intentionally
        // rejected — silently truncating an address is a footgun, so the FFI
        // layer raises UNSUPPORTED_TYPE instead.
        case NativeType::PTR:
        case NativeType::USIZE:
        case NativeType::ISIZE:
            *((PtrSize *) out) = self->ptr.load(std::memory_order_relaxed);

            return true;

        default:
            return false;
    }
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
// INTERNAL
// *********************************************************************************************************************

/// @brief Guard for the dereferencing methods.
///
/// Returns true when @p addr is safe to read/write. When @p addr is null it
/// raises a `RuntimeError` and returns false, so the call site can bail with a
/// single `if (!RawPtrCheckNonNull(...)) return {};`.
static bool RawPtrCheckNonNull(orbiter::Isolate *isolate, const PtrSize addr) {
    if (addr != 0)
        return true;

    ErrorSet(isolate,
             RuntimeError::Details[RuntimeError::Reason::ID],
             nullptr,
             "null pointer dereference");

    return false;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_FUNCTION(rawptr_alloc, alloc,
                 R"DOC(
@brief Allocate a heap buffer and return a RawPtr to it.

Allocates `size` bytes on the heap, fills every byte with `fill`, and wraps the
address in a RawPtr. This is the entry point for handing a writable buffer to a
native function — e.g. an out-parameter `int *`: allocate a cell, pass the
RawPtr to the callee, read the value back, then release it.

The buffer is owned by the caller and must be released with `free` when no
longer needed. For a garbage-collected alternative that never leaks, pass a
`Bytes` value to the native call instead.

@param size    Number of bytes to allocate. Must be positive.
@param fill=0  Byte value written to every byte of the buffer (low 8 bits used).

@return A RawPtr to the freshly allocated buffer.

@panic TypeError   When `size` or `fill` is not an Int.
@panic ValueError  When `size` is not positive.
@panic OOMError    When the allocation fails.

@see free, write_i32, read_i32, read_ptr

@example
    # int out; native_fn(&out)
    let p = RawPtr.alloc(4)
    native_fn(p)
    let out = p.read_i32()
    p.free()
)DOC", 1, "fill", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("size", false, InstanceType::NUMBER),
                   PCHECK_DEF("fill", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    IntegerUnderlying size;
    if (!NumberExtract(argv[0], size))
        return {};

    if (size <= 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "alloc size must be positive");

        return {};
    }

    IntegerUnderlying fill = 0;
    if (!O_IS_SENTINEL(argv[1]) && !NumberExtract(argv[1], fill))
        return {};

    auto *buffer = ::malloc(size);
    if (buffer == nullptr) {
        ErrorSet(isolate,
                 MemoryError::Details[MemoryError::Reason::ID],
                 nullptr,
                 MemoryError::Details[MemoryError::Reason::NATIVE_ALLOC]);

        return {};
    }

    ::memset(buffer, (unsigned char) fill, size);

    auto p = RawPtrNew(isolate, buffer);
    if (!p) {
        ::free(buffer);

        return {};
    }

    return HOObject(std::move(p));
}

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

Supports negative offsets to move backwards. When `word` is true the count
is measured in pointer-size units (each `sizeof(void *)` bytes) instead of
raw bytes.

@param bytes  Signed count to add to the address (bytes, or pointer-size
              units when `word` is true).
@param word?  When true, scale `bytes` by the platform pointer size.
              Defaults to false.

@return nil

@see sub, offset
)DOC", 2, "word", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("bytes", false, InstanceType::NUMBER),
                   PCHECK_DEF("word", true, InstanceType::BOOLEAN));
    PCHECK_CHECK(params);

    auto *self = (RawPtr *) argv[0];

    IntegerUnderlying bytes;
    NumberExtract(argv[1], bytes);

    if (!O_IS_SENTINEL(argv[2]) && OBOOL_TO_BOOL(argv[2]))
        bytes *= (IntegerUnderlying) sizeof(void *);

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

Supports negative offsets to move backwards. When `word` is true the count
is measured in pointer-size units (each `sizeof(void *)` bytes) instead of
raw bytes — convenient for walking a `T **` such as an `argv`-style array
without hardcoding the platform word size.

@param bytes  Signed count to add to the address (bytes, or pointer-size
              units when `word` is true).
@param word?  When true, scale `bytes` by the platform pointer size.
              Defaults to false.

@return A new RawPtr pointing to the advanced address.

@panic TypeError  When `bytes` is not an integer.

@see add, sub, address, read_ptr
)DOC", 2, "word", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("bytes", false, InstanceType::NUMBER),
                   PCHECK_DEF("word", true, InstanceType::BOOLEAN));
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    IntegerUnderlying bytes;
    NumberExtract(argv[1], bytes);

    if (!O_IS_SENTINEL(argv[2]) && OBOOL_TO_BOOL(argv[2]))
        bytes *= (IntegerUnderlying) sizeof(void *);

    const auto addr = self->ptr.load(std::memory_order_relaxed);
    auto p = RawPtrNew(isolate, (void *) (addr + bytes));
    if (!p)
        return {};

    return HOObject(std::move(p));
}

RUNTIME_METHOD(rawptr_read_bytes, read_bytes,
               R"DOC(
@brief Read `size` bytes from the pointed-to address and return them as a Bytes object.

Exactly `size` bytes are copied verbatim into a new, mutable Bytes; the pointer
is not retained. Unlike `read_string`, `size` is mandatory — a raw memory region
has no terminator to infer its length from.

@param size  Number of bytes to read. Must be non-negative.

@return A new Bytes object containing the copied data.

@panic RuntimeError  When the pointer is null.
@panic ValueError    When `size` is negative.

@see read_string, read_i8, read_i64
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("size", false, InstanceType::NUMBER)
    );
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (!RawPtrCheckNonNull(isolate, addr))
        return {};

    IntegerUnderlying size;
    NumberExtract(argv[1], size);

    if (size < 0) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "size cannot be negative");

        return {};
    }

    auto bytes = BytesNew(isolate, (const unsigned char *) addr, size, false);
    if (!bytes)
        return {};

    return HOObject(std::move(bytes));
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

    if (!RawPtrCheckNonNull(isolate, addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(isolate, addr))
        return {};

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

    if (!RawPtrCheckNonNull(isolate, addr))
        return {};

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

    if (!RawPtrCheckNonNull(isolate, addr))
        return {};

    void *val;

    memcpy(&val, (void *) addr, sizeof(val));

    auto p = RawPtrNew(isolate, val);
    if (!p)
        return {};

    return HOObject(std::move(p));
}

RUNTIME_METHOD(rawptr_read_string, read_string,
               R"DOC(
@brief Read a NUL-terminated or fixed-length string from the pointed-to address.

When `length` is omitted the bytes are read up to (but not including) the first
NUL terminator, like C `strlen`. When `length` is given, exactly that many bytes
are copied verbatim — embedded NUL bytes included.

The bytes are copied into a new String; the pointer is not retained.

@param length?  Number of bytes to read. Omit to read up to the first NUL.

@return A new String containing the copied bytes.

@panic RuntimeError  When the pointer is null.
@panic ValueError    When `length` is negative.

@see read_bytes, read_ptr
)DOC", 1, "length", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("length", true, InstanceType::NUMBER)
    );
    PCHECK_CHECK(params);

    const auto *self = (const RawPtr *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    const auto addr = self->ptr.load(std::memory_order_relaxed);

    if (!RawPtrCheckNonNull(isolate, addr))
        return {};

    HORString str;

    if (!O_IS_SENTINEL(argv[1])) {
        IntegerUnderlying length;
        NumberExtract(argv[1], length);

        if (length < 0) {
            ErrorSet(isolate,
                     ValueError::Details[ValueError::Reason::ID],
                     nullptr,
                     "length cannot be negative");

            return {};
        }

        str = ORStringNew(isolate, (const char *) addr, length);
    } else
        str = ORStringNew(isolate, (const char *) addr);

    if (!str)
        return {};

    return HOObject(std::move(str));
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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(isolate, addr))
        return {};

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

    if (!RawPtrCheckNonNull(isolate, addr))
        return {};

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

Supports negative offsets to move forwards. When `word` is true the count
is measured in pointer-size units (each `sizeof(void *)` bytes) instead of
raw bytes.

@param bytes  Signed count to subtract from the address (bytes, or
              pointer-size units when `word` is true).
@param word?  When true, scale `bytes` by the platform pointer size.
              Defaults to false.

@return nil

@see add, offset
)DOC", 2, "word", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RAWPTR),
                   PCHECK_DEF("bytes", false, InstanceType::NUMBER),
                   PCHECK_DEF("word", true, InstanceType::BOOLEAN));
    PCHECK_CHECK(params);

    auto *self = (RawPtr *) argv[0];

    IntegerUnderlying bytes;
    NumberExtract(argv[1], bytes);

    if (!O_IS_SENTINEL(argv[2]) && OBOOL_TO_BOOL(argv[2]))
        bytes *= (IntegerUnderlying) sizeof(void *);

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

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

    if (!RawPtrCheckNonNull(O_GET_ISOLATE(self), addr))
        return {};

    IntegerUnderlying value;
    NumberExtract(argv[1], value);

    const auto val = (uint64_t) value;
    memcpy((void *) addr, &val, sizeof(val));

    return HOObject(kOddBallNIL);
}

constexpr FunctionDef rawptr_methods[] = {
    rawptr_address,
    rawptr_add,
    rawptr_alloc,
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
    rawptr_read_string,
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
    ops.to_native = (ToNativeType) RawPtrToNative;
    ops.to_string = RawPtrToString;
    ops.hash = RawPtrHash;

    return TIPropertyAdd(self, rawptr_methods, PropertyFlag::IS_PUBLIC);
}

HOType orbiter::datatype::RawPtrTypeInit(Isolate *isolate) {
    auto rawptr = MakeType(isolate, "Rawptr", InstanceType::RAWPTR, sizeof(RawPtr) - sizeof(OObject), 28, 0);
    return rawptr;
}

HRawPtr orbiter::datatype::RawPtrNew(Isolate *isolate, void *ptr) {
    auto *rawptr = MakeObject<RawPtr>(isolate, InstanceType::RAWPTR);
    if (rawptr != nullptr)
        rawptr->ptr.store((PtrSize) ptr, std::memory_order_relaxed);

    O_GC_TRACK_RETURN(isolate, rawptr, false);
}
