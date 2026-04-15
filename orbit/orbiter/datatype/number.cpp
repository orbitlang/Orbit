// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <numeric>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/datatype/number.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// TYPE OPS HELPERS
// *********************************************************************************************************************

/// Return the integer value of any numeric OObject (Number heap or SMI).
/// Precondition: `obj` must be a Number or SMI (guaranteed by method dispatch / PCHECK).
static IntegerUnderlying NumberVal(const OObject *obj) {
    if (O_IS_SMI(obj))
        return ((MSSize) (MSize) obj >> 1);

    return ((const Number *) obj)->sint;
}

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

static int NumberCompare(const OObject *left, const OObject *right) {
    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return 0;

    if (a < b) return -1;
    if (a > b) return 1;

    return 0;
}

static bool NumberEqual(const OObject *left, const OObject *right) {
    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    return a == b;
}

// *********************************************************************************************************************
// TYPE OPS — ARITHMETIC
// *********************************************************************************************************************

static bool NumberAdd(const OObject *left, const OObject *right, OObject *&result) {
    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    const auto n = IntNew(orbiter::Fiber::Current()->isolate, a + b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool NumberSub(const OObject *left, const OObject *right, OObject *&result) {
    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    const auto n = IntNew(orbiter::Fiber::Current()->isolate, a - b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool NumberMul(const OObject *left, const OObject *right, OObject *&result) {
    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    const auto n = IntNew(orbiter::Fiber::Current()->isolate, a * b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

/// True float division: always returns a Decimal so that 5 / 2 == 2.5.
static bool NumberDiv(const OObject *left, const OObject *right, OObject *&result) {
    auto *isolate = orbiter::Fiber::Current()->isolate;

    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    if (b == 0) [[unlikely]] {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::ZERO_DIVISION]);

        return false;
    }

    const auto n = DecimalNew(isolate, (DecimalUnderlying) a / (DecimalUnderlying) b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

/// Floor (integer) division: ⌊a / b⌋ — rounds towards negative infinity.
static bool NumberIDiv(const OObject *left, const OObject *right, OObject *&result) {
    auto *isolate = orbiter::Fiber::Current()->isolate;

    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    if (b == 0) [[unlikely]] {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::ZERO_DIVISION]);

        return false;
    }

    // C++ truncates towards zero; adjust towards -∞ when the signs differ and
    // there is a non-zero remainder.
    auto q = a / b;
    const auto r = a % b;
    if (r != 0 && ((r ^ b) < 0))
        q--;

    const auto n = IntNew(isolate, q);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool NumberMod(const OObject *left, const OObject *right, OObject *&result) {
    auto *isolate = orbiter::Fiber::Current()->isolate;

    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    if (b == 0) [[unlikely]] {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::ZERO_DIVISION]);

        return false;
    }

    const auto n = IntNew(isolate, a % b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — BITWISE
// *********************************************************************************************************************

static bool NumberAnd(const OObject *left, const OObject *right, OObject *&result) {
    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    const auto n = IntNew(orbiter::Fiber::Current()->isolate, a & b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool NumberOr(const OObject *left, const OObject *right, OObject *&result) {
    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    const auto n = IntNew(orbiter::Fiber::Current()->isolate, a | b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool NumberXor(const OObject *left, const OObject *right, OObject *&result) {
    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    const auto n = IntNew(orbiter::Fiber::Current()->isolate, a ^ b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool NumberLShift(const OObject *left, const OObject *right, OObject *&result) {
    auto *isolate = orbiter::Fiber::Current()->isolate;

    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    if (b < 0) [[unlikely]] {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::NEGATIVE_SHIFT_COUNT]);

        return false;
    }

    // Large shift → 0, mirroring the SMI fast path in oops.cpp.
    const IntegerUnderlying shifted = (b >= (sizeof(IntegerUnderlying) * 8))
                                          ? 0
                                          : (IntegerUnderlying) ((UIntegerUnderlying) a << b);

    const auto n = IntNew(isolate, shifted);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool NumberRShift(const OObject *left, const OObject *right, OObject *&result) {
    auto *isolate = orbiter::Fiber::Current()->isolate;

    IntegerUnderlying a, b;

    if (!NumberExtract(left, a) || !NumberExtract(right, b))
        return false;

    if (b < 0) [[unlikely]] {
        ErrorSet(isolate,
                 RuntimeError::Details[RuntimeError::Reason::ID],
                 nullptr,
                 RuntimeError::Details[RuntimeError::Reason::NEGATIVE_SHIFT_COUNT]);

        return false;
    }

    // Cap the shift to the sign-bit position to preserve arithmetic right-shift
    // semantics (negative values stay negative), mirroring the SMI fast path.
    const IntegerUnderlying shift = (b < (sizeof(IntegerUnderlying) * 8))
                                        ? b
                                        : (IntegerUnderlying) (sizeof(IntegerUnderlying) * 8) - 1;

    const auto n = IntNew(isolate, a >> shift);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — UNARY
// *********************************************************************************************************************

static bool NumberNeg(const OObject *self, OObject *&result) {
    const auto n = IntNew(O_GET_ISOLATE(self), -((const Number *) self)->sint);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool NumberBitNot(const OObject *self, OObject *&result) {
    const auto n = IntNew(O_GET_ISOLATE(self), ~((const Number *) self)->sint);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

static bool NumberToBool(const OObject *self) {
    return ((const Number *) self)->sint != 0;
}

/// Formats the value as a signed decimal integer.
static OObject *NumberToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto s = ORStringFormat(isolate, "%lld", ((const Number *) self)->sint);
    return s ? (OObject *) s.get() : nullptr;
}

static bool NumberToNative(const OObject *self, void *out, const NativeType type) {
    const auto v = ((const Number *) self)->sint;

    switch (type) {
        case NativeType::BOOL:
            *((bool *) out) = v != 0;
            return true;
        case NativeType::BYTE:
            *((U8 *) out) = (U8) v;
            return true;
        case NativeType::I8:
            *((char *) out) = (char) v;
            return true;
        case NativeType::I16:
            *((I16 *) out) = (I16) v;
            return true;
        case NativeType::I32:
            *((I32 *) out) = (I32) v;
            return true;
        case NativeType::I64:
            *((I64 *) out) = (I64) v;
            return true;
        case NativeType::ISIZE:
            *((MSSize *) out) = (MSSize) v;
            return true;
        case NativeType::U8:
            *((U8 *) out) = (U8) v;
            return true;
        case NativeType::U16:
            *((U16 *) out) = (U16) v;
            return true;
        case NativeType::U32:
            *((U32 *) out) = (U32) v;
            return true;
        case NativeType::U64:
            *((U64 *) out) = (I64) v;
            return true;
        case NativeType::USIZE:
            *((MSize *) out) = (MSize) v;
            return true;
        case NativeType::F32:
            *((float *) out) = (float) v;
            return true;
        case NativeType::F64:
            *((double *) out) = (double) v;
            return true;
        default:
            return false;
    }
}

// *********************************************************************************************************************
// TYPE OPS — RUNTIME
// *********************************************************************************************************************

/// An integer hashes to its own value — fast and collision-free.
static MSize NumberHash(const OObject *self) {
    return ((const Number *) self)->sint;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_METHOD(number_abs, abs,
               R"DOC(
@brief Return the absolute value of self.

@return A new Int with the non-negative magnitude of self.

@see clamp

@example
    (-7).abs()    // 7
    (3).abs()     // 3
)DOC", 1, false, false) {
    const auto v = NumberVal(argv[0]);

    auto n = IntNew(O_GET_ISOLATE(_func), v < 0 ? -v : v);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(number_bit_length, bit_length,
               R"DOC(
@brief Return the number of bits required to represent the absolute value of self.

Zero requires 0 bits.

@return A new Int in the range [0, 63].

@example
    (0).bit_length()     // 0
    (1).bit_length()     // 1
    (255).bit_length()   // 8
    (-128).bit_length()  // 7
)DOC", 1, false, false) {
    auto v = NumberVal(argv[0]);

    if (v < 0) v = -v;

    IntegerUnderlying bits = 0;

    while (v != 0) {
        v >>= 1;
        bits++;
    }

    auto n = IntNew(O_GET_ISOLATE(_func), bits);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(number_clamp, clamp,
               R"DOC(
@brief Clamp self to the inclusive range [lo, hi].

@param lo  Lower bound of the range.
@param hi  Upper bound of the range.

@return lo if self < lo, hi if self > hi, otherwise self.

@panic TypeError  When `lo` or `hi` is not an Int.

@see min, max

@example
    (5).clamp(0, 10)    // 5
    (-1).clamp(0, 10)   // 0
    (12).clamp(0, 10)   // 10
)DOC", 3, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("lo", false, InstanceType::NUMBER),
                   PCHECK_DEF("hi", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto v = NumberVal(argv[0]);
    const auto lo = NumberVal(argv[1]);
    const auto hi = NumberVal(argv[2]);

    const auto clamped = v < lo ? lo : (v > hi ? hi : v);

    auto n = IntNew(O_GET_ISOLATE(_func), clamped);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(number_gcd, gcd,
               R"DOC(
@brief Return the greatest common divisor of self and other.

Delegates to std::gcd (C++17 <numeric>).  The result is always
non-negative; negative inputs are treated as their absolute values.

@param other  The second operand.

@return A new non-negative Int equal to gcd(|self|, |other|).

@panic TypeError  When `other` is not an Int.

@example
    (12).gcd(8)     // 4
    (0).gcd(5)      // 5
    (-6).gcd(9)     // 3
)DOC", 2, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("other", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto a = NumberVal(argv[0]);
    const auto b = NumberVal(argv[1]);

    auto n = IntNew(O_GET_ISOLATE(_func), std::gcd(a, b));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(number_is_even, is_even,
               R"DOC(
@brief Return true when self is divisible by two.

@return true or false.

@see is_odd

@example
    (4).is_even()    // true
    (7).is_even()    // false
)DOC", 1, false, false) {
    return HOObject((OObject *) BOOL_TO_OBOOL((NumberVal(argv[0]) & 1) == 0));
}

RUNTIME_METHOD(number_is_odd, is_odd,
               R"DOC(
@brief Return true when self is not divisible by two.

@return true or false.

@see is_even

@example
    (3).is_odd()    // true
    (8).is_odd()    // false
)DOC", 1, false, false) {
    return HOObject((OObject *) BOOL_TO_OBOOL((NumberVal(argv[0]) & 1) != 0));
}

RUNTIME_METHOD(number_max, max,
               R"DOC(
@brief Return the larger of self and other.

@param other  The value to compare against.

@return self if self >= other, otherwise other.

@panic TypeError  When `other` is not an Int.

@see min, clamp

@example
    (3).max(7)    // 7
    (5).max(2)    // 5
)DOC", 2, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("other", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto a = NumberVal(argv[0]);
    const auto b = NumberVal(argv[1]);

    auto n = IntNew(O_GET_ISOLATE(_func), a >= b ? a : b);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(number_min, min,
               R"DOC(
@brief Return the smaller of self and other.

@param other  The value to compare against.

@return self if self <= other, otherwise other.

@panic TypeError  When `other` is not an Int.

@see max, clamp

@example
    (3).min(7)    // 3
    (5).min(2)    // 2
)DOC", 2, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("other", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    const auto a = NumberVal(argv[0]);
    const auto b = NumberVal(argv[1]);

    auto n = IntNew(O_GET_ISOLATE(_func), a <= b ? a : b);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(number_pow, pow,
               R"DOC(
@brief Return self raised to the power of exp.

Uses fast binary exponentiation.  Negative exponents return 0
(consistent with integer semantics).

@param exp  Non-negative integer exponent.

@return A new Int equal to self^exp.

@panic TypeError  When `exp` is not an Int.

@see sqrt

@example
    (2).pow(10)    // 1024
    (3).pow(0)     // 1
    (5).pow(-1)    // 0
)DOC", 2, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("exp", false, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    auto base = NumberVal(argv[0]);
    auto exp = NumberVal(argv[1]);

    if (exp < 0) {
        auto n = IntNew(orbiter::Fiber::Current()->isolate, 0);
        if (!n)
            return {};

        return HOObject(std::move(n));
    }

    IntegerUnderlying result = 1;

    while (exp > 0) {
        if (exp & 1)
            result *= base;

        base *= base;

        exp >>= 1;
    }

    auto n = IntNew(orbiter::Fiber::Current()->isolate, result);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(number_sqrt, sqrt,
               R"DOC(
@brief Return the square root of self as a Decimal.

Returns NaN when self is negative, following IEEE 754 semantics.

@return A new Decimal equal to √self.

@see pow

@example
    (9).sqrt()     // 3.0
    (2).sqrt()     // 1.4142135623...
    (-1).sqrt()    // nan
)DOC", 1, false, false) {
    const auto v = (DecimalUnderlying) NumberVal(argv[0]);

    auto n = DecimalNew(orbiter::Fiber::Current()->isolate, sqrtl(v));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(number_str, str,
               R"DOC(
@brief Return a string representation of self.

Overrides the base Type str() method.

@return A String containing the decimal representation of self.

@see repr

@example
    (42).str()      // "42"
    (-7).str()      // "-7"
)DOC", 1, false, false) {
    return ToString(orbiter::Fiber::Current()->isolate, argv[0]);
}

constexpr FunctionDef number_methods[] = {
    number_abs,
    number_bit_length,
    number_clamp,
    number_gcd,
    number_is_even,
    number_is_odd,
    number_max,
    number_min,
    number_pow,
    number_sqrt,
    number_str,

    FUNCTIONDEF_SENTINEL
};

bool orbiter::datatype::NumberExtract(const OObject *obj, IntegerUnderlying &out) {
    if (O_IS_SMI(obj)) {
        out = (IntegerUnderlying) ((MSSize) (MSize) obj >> 1);

        return true;
    }

    if (O_IS_OBJECT(obj) && O_IS_TYPE(obj, InstanceType::NUMBER)) {
        out = ((const Number *) obj)->sint;

        return true;
    }

    return false;
}

bool orbiter::datatype::NumberTypeSetup(TypeInfo *self) {
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.compare = NumberCompare;
    ops.equal = NumberEqual;
    ops.add = NumberAdd;
    ops.sub = NumberSub;
    ops.mul = NumberMul;
    ops.div = NumberDiv;
    ops.idiv = NumberIDiv;
    ops.mod = NumberMod;
    ops.bit_and = NumberAnd;
    ops.bit_or = NumberOr;
    ops.bit_xor = NumberXor;
    ops.lshift = NumberLShift;
    ops.rshift = NumberRShift;
    ops.neg = NumberNeg;
    ops.bit_not = NumberBitNot;
    ops.to_bool = NumberToBool;
    ops.to_string = NumberToString;
    ops.to_repr = NumberToString;
    ops.to_native = (ToNativeType) NumberToNative;
    ops.hash = NumberHash;

    return TIPropertyAdd(self, number_methods, PropertyFlag::IS_PUBLIC);
}

HNumber orbiter::datatype::IntNew(Isolate *isolate, const IntegerUnderlying value) {
    if (value >= kSMIMinSize && value <= kSMIMaxSize)
        return HNumber((Number *) O_TO_SMI(value));

    auto *num = MakeObject<Number>(isolate, InstanceType::NUMBER);
    if (num == nullptr)
        return {};

    num->sint = value;

    O_GC_TRACK_RETURN(isolate, num, false);
}

HNumber orbiter::datatype::IntNew(Isolate *isolate, const char *string, int base) {
    const auto num = std::strtol(string, nullptr, base);

    return IntNew(isolate, num);
}

HNumber orbiter::datatype::UIntNew(Isolate *isolate, const UIntegerUnderlying value) {
    auto *num = MakeObject<Number>(isolate, InstanceType::NUMBER);
    if (num == nullptr)
        return {};

    num->uint = value;

    O_GC_TRACK_RETURN(isolate, num, false);
}

HNumber orbiter::datatype::UIntNew(Isolate *isolate, const char *string, int base) {
    const auto num = std::strtoul(string, nullptr, base);

    return UIntNew(isolate, num);
}

HOType orbiter::datatype::NumberTypeInit(Isolate *isolate) {
    auto number = MakeType(isolate, "Number", InstanceType::NUMBER, sizeof(Number) - sizeof(OObject), 11, 0);
    return number;
}
