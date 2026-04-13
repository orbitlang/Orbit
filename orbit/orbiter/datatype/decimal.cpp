// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cmath>
#include <cstdlib>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/datatype/decimal.h>

using namespace orbiter::datatype;

RUNTIME_METHOD(decimal_abs, abs,
               R"DOC(
@brief Return the absolute value of the decimal.

@return A new Decimal with the non-negative magnitude of self.

@see clamp

@example
    (-3.14).abs()   // 3.14
    (2.71).abs()    // 2.71
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 3, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("lo", false, InstanceType::DECIMAL),
                   PCHECK_DEF("hi", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto v  = ((Decimal *) argv[0])->value;
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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 2, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("base", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto x    = ((Decimal *) argv[0])->value;
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
)DOC", 2, false, false) {
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
)DOC", 2, false, false) {
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
)DOC", 2, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("exp", false, InstanceType::DECIMAL));
    PCHECK_CHECK(params);

    const auto base = ((Decimal *) argv[0])->value;
    const auto exp  = ((Decimal *) argv[1])->value;

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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
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
)DOC", 1, false, false) {
    auto s = ORStringFormat(O_GET_ISOLATE(_func), "%.10Lg", ((Decimal *) argv[0])->value);
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(decimal_tan, tan,
               R"DOC(
@brief Return the tangent of self (in radians).

Returns ±infinity at odd multiples of π/2, following IEEE 754 semantics.

@return A new Decimal.

@see sin, cos

@example
    (0.0).tan()    // 0.0
)DOC", 1, false, false) {
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
    auto decimal = MakeType(isolate, "Decimal", InstanceType::DECIMAL, sizeof(Decimal) - sizeof(OObject), 19, 0);
    return decimal;
}
