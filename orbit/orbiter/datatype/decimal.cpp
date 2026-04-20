// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cmath>
#include <cstring>
#include <cstdlib>

#include <orbit/util/hash.h>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/datatype/decimal.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// TYPE OPS HELPERS
// *********************************************************************************************************************

/// Extract a DecimalUnderlying from any numeric object (Decimal, Number heap object, or SMI).
/// Returns false when `obj` is not a numeric type — the caller should return false to trigger
/// the NotImplementedError path in the dispatcher.
static bool DecimalExtract(const OObject *obj, DecimalUnderlying &out) {
    if (O_IS_SMI(obj)) {
        out = (DecimalUnderlying) ((MSSize) (MSize) obj >> 1);

        return true;
    }

    if (O_IS_OBJECT(obj)) {
        if (O_IS_TYPE(obj, InstanceType::DECIMAL)) {
            out = ((const Decimal *) obj)->value;

            return true;
        }

        if (O_IS_TYPE(obj, InstanceType::NUMBER)) {
            out = (DecimalUnderlying) ((const Number *) obj)->sint;

            return true;
        }
    }

    return false;
}

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

static int DecimalCompare(const OObject *left, const OObject *right) {
    DecimalUnderlying a, b;

    if (!DecimalExtract(left, a) || !DecimalExtract(right, b))
        return 0;

    if (a < b) return -1;
    if (a > b) return 1;

    return 0;
}

static bool DecimalEqual(const OObject *left, const OObject *right) {
    DecimalUnderlying a, b;

    if (!DecimalExtract(left, a) || !DecimalExtract(right, b))
        return false;

    // NaN != NaN per IEEE 754
    return a == b;
}

// *********************************************************************************************************************
// TYPE OPS — ARITHMETIC
// *********************************************************************************************************************

static bool DecimalAdd(const OObject *left, const OObject *right, OObject *&result) {
    DecimalUnderlying a, b;

    if (!DecimalExtract(left, a) || !DecimalExtract(right, b))
        return false;

    const auto n = DecimalNew(orbiter::Fiber::Current()->isolate, a + b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool DecimalSub(const OObject *left, const OObject *right, OObject *&result) {
    DecimalUnderlying a, b;

    if (!DecimalExtract(left, a) || !DecimalExtract(right, b))
        return false;

    const auto n = DecimalNew(orbiter::Fiber::Current()->isolate, a - b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool DecimalMul(const OObject *left, const OObject *right, OObject *&result) {
    DecimalUnderlying a, b;

    if (!DecimalExtract(left, a) || !DecimalExtract(right, b))
        return false;

    const auto n = DecimalNew(orbiter::Fiber::Current()->isolate, a * b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool DecimalDiv(const OObject *left, const OObject *right, OObject *&result) {
    DecimalUnderlying a, b;

    if (!DecimalExtract(left, a) || !DecimalExtract(right, b))
        return false;

    const auto n = DecimalNew(orbiter::Fiber::Current()->isolate, a / b);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

/// Integer (floor) division: ⌊a / b⌋ — rounds towards negative infinity.
static bool DecimalIDiv(const OObject *left, const OObject *right, OObject *&result) {
    DecimalUnderlying a, b;

    if (!DecimalExtract(left, a) || !DecimalExtract(right, b))
        return false;

    const auto n = DecimalNew(orbiter::Fiber::Current()->isolate, floorl(a / b));
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

static bool DecimalMod(const OObject *left, const OObject *right, OObject *&result) {
    DecimalUnderlying a, b;

    if (!DecimalExtract(left, a) || !DecimalExtract(right, b))
        return false;

    const auto n = DecimalNew(orbiter::Fiber::Current()->isolate, fmodl(a, b));
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — UNARY
// *********************************************************************************************************************

static bool DecimalNeg(const OObject *self, OObject *&result) {
    const auto n = DecimalNew(O_GET_ISOLATE(self), -((const Decimal *) self)->value);
    if (!n) return false;

    result = (OObject *) n.get();

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

static bool DecimalToBool(const OObject *self) {
    return ((const Decimal *) self)->value != 0.0L;
}

/// Formats the value with up to 10 significant digits (trailing zeros removed).
static OObject *DecimalToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto s = ORStringFormat(isolate, "%.10Lg", ((const Decimal *) self)->value);
    return s ? (OObject *) s.get() : nullptr;
}

static bool DecimalToNative(const OObject *self, void *out, const NativeType type) {
    const auto v = ((const Decimal *) self)->value;

    switch (type) {
        case NativeType::F32:
            *((float *) out) = (float) v;
            break;
        case NativeType::F64:
            *((double *) out) = (double) v;
            break;
        default:
            return false;
    }

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — RUNTIME
// *********************************************************************************************************************

/// FNV-1a hash over the raw bytes of the long double value.
/// NaN is canonicalised to hash 0 so that all NaN representations collide consistently.
static MSize DecimalHash(const OObject *self) {
    const auto v = ((const Decimal *) self)->value;

    if (std::isnan(v))
        return 0;

    return fnv1_hash((unsigned char *) &v, sizeof(DecimalUnderlying));
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_METHOD(decimal_abs, abs,
               R"DOC(
@brief Return the absolute value of the decimal.

@return A new Decimal with the non-negative magnitude of self.

@see clamp

@example
    (-3.14).abs()   // 3.14
    (2.71).abs()    // 2.71
)DOC", 1, nullptr, false, false) {
    auto n = DecimalNew(O_GET_ISOLATE(_func), fabsl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_ceil, ceil,
               R"DOC(
@brief Return the smallest integer greater than or equal to self.

@return An Int equal to the ceiling of self.

@see floor, round, to_int

@example
    (2.1).ceil()    // 3
    (-2.9).ceil()   // -2
)DOC", 1, nullptr, false, false) {
    auto n = IntNew(O_GET_ISOLATE(_func), (IntegerUnderlying) ceill(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_clamp, clamp,
               R"DOC(
@brief Clamp self to the inclusive range [lo, hi].

@param lo  Lower bound of the range.
@param hi  Upper bound of the range.

@return lo if self < lo, hi if self > hi, otherwise self.

@panic TypeError  When `lo` or `hi` is not a Decimal.

@see min, max

@example
    (5.0).clamp(0.0, 10.0)    // 5.0
    (-1.0).clamp(0.0, 10.0)   // 0.0
    (12.0).clamp(0.0, 10.0)   // 10.0
)DOC", 3, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("lo", false, InstanceType::DECIMAL),
                   PCHECK_DEF("hi", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto v = ((Decimal *) argv[0])->value;
    const auto lo = ((Decimal *) argv[1])->value;
    const auto hi = ((Decimal *) argv[2])->value;

    const auto clamped = v < lo ? lo : (v > hi ? hi : v);

    auto n = DecimalNew(O_GET_ISOLATE(_func), clamped);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_cos, cos,
               R"DOC(
@brief Return the cosine of self (in radians).

@return A new Decimal in the range [-1.0, 1.0].

@see sin, tan

@example
    (0.0).cos()    // 1.0
)DOC", 1, nullptr, false, false) {
    auto n = DecimalNew(O_GET_ISOLATE(_func), cosl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_exp, exp,
               R"DOC(
@brief Return e raised to the power of self.

@return A new Decimal equal to e^self.

@see ln, pow

@example
    (0.0).exp()    // 1.0
    (1.0).exp()    // 2.718281828...
)DOC", 1, nullptr, false, false) {
    auto n = DecimalNew(O_GET_ISOLATE(_func), expl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_floor, floor,
               R"DOC(
@brief Return the largest integer less than or equal to self.

@return An Int equal to the floor of self.

@see ceil, round, to_int

@example
    (2.9).floor()    // 2
    (-2.1).floor()   // -3
)DOC", 1, nullptr, false, false) {
    auto n = IntNew(O_GET_ISOLATE(_func), (IntegerUnderlying) floorl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_is_finite, is_finite,
               R"DOC(
@brief Return true if self is a finite number (not NaN and not infinity).

@return true if self is finite, false otherwise.

@see is_nan, is_inf

@example
    (3.14).is_finite()       // true
    (1.0 / 0.0).is_finite()  // false
)DOC", 1, nullptr, false, false) {
    return HOObject((OObject *) BOOL_TO_OBOOL(std::isfinite(((Decimal *) argv[0])->value)));
}

RUNTIME_METHOD(decimal_is_inf, is_inf,
               R"DOC(
@brief Return true if self is positive or negative infinity.

@return true if self is ±infinity, false otherwise.

@see is_nan, is_finite

@example
    (1.0 / 0.0).is_inf()    // true
    (1.0).is_inf()           // false
)DOC", 1, nullptr, false, false) {
    return HOObject((OObject *) BOOL_TO_OBOOL(std::isinf(((Decimal *) argv[0])->value)));
}

RUNTIME_METHOD(decimal_is_nan, is_nan,
               R"DOC(
@brief Return true if self is NaN (not a number).

@return true if self is NaN, false otherwise.

@see is_inf, is_finite

@example
    (0.0 / 0.0).is_nan()    // true
    (1.0).is_nan()           // false
)DOC", 1, nullptr, false, false) {
    return HOObject((OObject *) BOOL_TO_OBOOL(std::isnan(((Decimal *) argv[0])->value)));
}

RUNTIME_METHOD(decimal_ln, ln,
               R"DOC(
@brief Return the natural logarithm (base e) of self.

Returns NaN for negative values and -infinity for 0.0, following IEEE 754 semantics.

@return A new Decimal equal to ln(self).

@see log, exp

@example
    (1.0).ln()              // 0.0
    (2.718281828).ln()      // ~1.0
)DOC", 1, nullptr, false, false) {
    auto n = DecimalNew(O_GET_ISOLATE(_func), logl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_log, log,
               R"DOC(
@brief Return the logarithm of self in the given base.

Computed as ln(self) / ln(base).  Returns NaN when base <= 0 or base == 1,
and NaN for negative self, following IEEE 754 semantics.

@param base  The logarithm base (must be positive and not equal to 1).

@return A new Decimal equal to log_base(self).

@panic TypeError  When `base` is not a Decimal.

@see ln, exp

@example
    (100.0).log(10.0)    // 2.0
    (8.0).log(2.0)       // 3.0
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("base", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto x = ((Decimal *) argv[0])->value;
    const auto base = ((Decimal *) argv[1])->value;

    auto n = DecimalNew(O_GET_ISOLATE(_func), logl(x) / logl(base));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_max, max,
               R"DOC(
@brief Return the larger of self and `other`.

@param other  The value to compare against.

@return self if self >= other, otherwise other.

@panic TypeError  When `other` is not a Decimal.

@see min, clamp

@example
    (3.0).max(5.0)    // 5.0
    (7.0).max(2.0)    // 7.0
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("other", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto a = ((Decimal *) argv[0])->value;
    const auto b = ((Decimal *) argv[1])->value;

    auto n = DecimalNew(O_GET_ISOLATE(_func), a > b ? a : b);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_min, min,
               R"DOC(
@brief Return the smaller of self and `other`.

@param other  The value to compare against.

@return self if self <= other, otherwise other.

@panic TypeError  When `other` is not a Decimal.

@see max, clamp

@example
    (3.0).min(5.0)    // 3.0
    (7.0).min(2.0)    // 2.0
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("other", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto a = ((Decimal *) argv[0])->value;
    const auto b = ((Decimal *) argv[1])->value;

    auto n = DecimalNew(O_GET_ISOLATE(_func), a < b ? a : b);
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_pow, pow,
               R"DOC(
@brief Return self raised to the power of `exp`.

@param exp  The exponent.

@return A new Decimal equal to self^exp.

@panic TypeError  When `exp` is not a Decimal.

@see sqrt, exp

@example
    (2.0).pow(10.0)    // 1024.0
    (9.0).pow(0.5)     // 3.0
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("exp", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto base = ((Decimal *) argv[0])->value;
    const auto exp = ((Decimal *) argv[1])->value;

    auto n = DecimalNew(O_GET_ISOLATE(_func), powl(base, exp));
    if (!n)
        return {};
    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_round, round,
               R"DOC(
@brief Return self rounded to the nearest integer, rounding half away from zero.

@return An Int equal to the rounded value.

@see floor, ceil, to_int

@example
    (2.5).round()    // 3
    (-2.5).round()   // -3
    (2.4).round()    // 2
)DOC", 1, nullptr, false, false) {
    auto n = IntNew(O_GET_ISOLATE(_func), (IntegerUnderlying) roundl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_sin, sin,
               R"DOC(
@brief Return the sine of self (in radians).

@return A new Decimal in the range [-1.0, 1.0].

@see cos, tan

@example
    (0.0).sin()           // 0.0
    (3.14159265).sin()    // ~0.0
)DOC", 1, nullptr, false, false) {
    auto n = DecimalNew(O_GET_ISOLATE(_func), sinl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_sqrt, sqrt,
               R"DOC(
@brief Return the square root of self.

Returns NaN when self is negative, following IEEE 754 semantics.

@return A new Decimal equal to √self.

@see pow, exp

@example
    (9.0).sqrt()    // 3.0
    (2.0).sqrt()    // 1.4142135623...
    (-1.0).sqrt()   // nan
)DOC", 1, nullptr, false, false) {
    auto n = DecimalNew(O_GET_ISOLATE(_func), sqrtl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

RUNTIME_METHOD(decimal_str, str,
               R"DOC(
@brief Return a string representation of self.

Overrides the base Type str() method.  Uses up to 10 significant digits
with trailing zeros removed.  Switches to scientific notation for very
large or very small values.

@return A String representation of self.

@see repr

@example
    (3.14).str()      // "3.14"
    (1000000.0).str() // "1e+06"
)DOC", 1, nullptr, false, false) {
    return ToString(O_GET_ISOLATE(_func), argv[0]);
}

RUNTIME_METHOD(decimal_tan, tan,
               R"DOC(
@brief Return the tangent of self (in radians).

Returns ±infinity at odd multiples of π/2, following IEEE 754 semantics.

@return A new Decimal.

@see sin, cos

@example
    (0.0).tan()    // 0.0
)DOC", 1, nullptr, false, false) {
    auto n = DecimalNew(O_GET_ISOLATE(_func), tanl(((Decimal *) argv[0])->value));
    if (!n)
        return {};

    return HOObject(std::move(n));
}

constexpr FunctionDef decimal_methods[] = {
    decimal_abs,
    decimal_ceil,
    decimal_clamp,
    decimal_cos,
    decimal_exp,
    decimal_floor,
    decimal_is_finite,
    decimal_is_inf,
    decimal_is_nan,
    decimal_ln,
    decimal_log,
    decimal_max,
    decimal_min,
    decimal_pow,
    decimal_round,
    decimal_sin,
    decimal_sqrt,
    decimal_str,
    decimal_tan,

    FUNCTIONDEF_SENTINEL
};

bool orbiter::datatype::DecimalTypeSetup(TypeInfo *self) {
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.compare = DecimalCompare;
    ops.equal = DecimalEqual;
    ops.add = DecimalAdd;
    ops.sub = DecimalSub;
    ops.mul = DecimalMul;
    ops.div = DecimalDiv;
    ops.idiv = DecimalIDiv;
    ops.mod = DecimalMod;
    ops.neg = DecimalNeg;
    ops.to_bool = DecimalToBool;
    ops.to_string = DecimalToString;
    ops.to_repr = DecimalToString;
    ops.to_native = (ToNativeType) DecimalToNative;
    ops.hash = DecimalHash;

    return TIPropertyAdd(self, decimal_methods, PropertyFlag::IS_PUBLIC);
}

HDecimal orbiter::datatype::DecimalNew(Isolate *isolate, const DecimalUnderlying number) {
    auto *decimal = MakeObject<Decimal>(isolate, InstanceType::DECIMAL);
    if (decimal != nullptr)
        decimal->value = number;

    O_GC_TRACK_RETURN(isolate, decimal, false);
}

HDecimal orbiter::datatype::DecimalNew(Isolate *isolate, const char *string) {
    auto *decimal = MakeObject<Decimal>(isolate, InstanceType::DECIMAL);
    if (decimal != nullptr)
        decimal->value = std::strtold(string, nullptr);

    O_GC_TRACK_RETURN(isolate, decimal, false);
}

HOType orbiter::datatype::DecimalTypeInit(Isolate *isolate) {
    auto decimal = MakeType(isolate, "Decimal", InstanceType::DECIMAL, sizeof(Decimal) - sizeof(OObject), 18, 0);
    return decimal;
}
