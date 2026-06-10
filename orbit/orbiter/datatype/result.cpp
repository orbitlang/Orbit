// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/datatype/result.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Two results are equal when they carry the same ok/error state and their
/// contained values are equal.
static bool ResultEqual(const OObject *left, const OObject *right) {
    if (left == right)
        return true;

    if (!O_IS_OBJECT(left) || !O_IS_OBJECT(right))
        return false;

    if (!O_IS_TYPE(left, InstanceType::RESULT) || !O_IS_TYPE(right, InstanceType::RESULT))
        return false;

    const auto *l = (const Result *) left;
    const auto *r = (const Result *) right;

    if (l->ok != r->ok)
        return false;

    return Equal(l->value, r->value);
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// An ok result is truthy; an error result is falsy.
static bool ResultToBool(const OObject *self) {
    return ((const Result *) self)->ok;
}

/// Format: "ok<TypeName>" or "error<TypeName>"
static OObject *ResultToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto *result = (const Result *) self;

    char type_name[24];
    GetTypeName(isolate, result->value, type_name, sizeof(type_name));

    const auto s = ORStringFormat(isolate, "%s<%s>",
                                  result->ok ? "ok" : "error",
                                  type_name);

    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_FUNCTION(result_ok, ok,
                 R"DOC(
@brief Wrap a value in a successful Result.

@param value  The value to store.

@return A new ok Result containing `value`.

@see error

@example
    let r = Result.ok(42)
    r.unwrap()    // 42
)DOC", 1, nullptr, false, false) {
    return HOObject(ResultNew(O_GET_ISOLATE(_func), argv[0], true));
}

RUNTIME_FUNCTION(result_error, error,
                 R"DOC(
@brief Wrap a value in an error Result.

@param value  The error value to store.

@return A new error Result containing `value`.

@see ok

@example
    let r = Result.error("something went wrong")
    r.unwrap_err()    // "something went wrong"
)DOC", 1, nullptr, false, false) {
    return HOObject(ResultNew(O_GET_ISOLATE(_func), argv[0], false));
}

RUNTIME_METHOD(result_unwrap, unwrap,
               R"DOC(
@brief Return the contained value, panicking if the result is an error.

Use this method when you are certain the result is ok.  Calling it on an
error result raises a TypeError.

@return The contained value.

@panic TypeError  When the result is not ok.

@see unwrap_err, unwrap_or

@example
    Result.ok(42).unwrap()      // 42
    Result.error(42).unwrap()   // panic!
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RESULT));
    PCHECK_CHECK(params);

    const auto *self = (const Result *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    if (!self->ok) {
        ErrorSet(isolate,
                 TypeError::Details[TypeError::Reason::ID],
                 (OObject *) self,
                 "called unwrap() on an error Result");

        return {};
    }

    return HOObject(self->value);
}

RUNTIME_METHOD(result_unwrap_err, unwrap_err,
               R"DOC(
@brief Return the contained value, panicking if the result is ok.

Use this method when you are certain the result is an error.  Calling it on
an ok result raises a TypeError.

@return The contained error value.

@panic TypeError  When the result is ok.

@see unwrap, unwrap_or

@example
    Result.error(42).unwrap_err()   // 42
    Result.ok(42).unwrap_err()      // panic!
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::RESULT));
    PCHECK_CHECK(params);

    const auto *self = (const Result *) argv[0];
    auto *isolate = O_GET_ISOLATE(self);

    if (self->ok) {
        ErrorSet(isolate,
                 TypeError::Details[TypeError::Reason::ID],
                 (OObject *) self,
                 "called unwrap_err() on an ok Result");

        return {};
    }

    return HOObject(self->value);
}

RUNTIME_METHOD(result_unwrap_or, unwrap_or,
               R"DOC(
@brief Return the contained value, or a default if the result is an error.

Unlike unwrap(), this method never panics.  When the result is an error the
provided `default` is returned instead.

@param default  The fallback value to return when the result is not ok.

@return The contained value, or `default`.

@see unwrap, unwrap_err

@example
    Result.ok(42).unwrap_or(0)      // 42
    Result.error(42).unwrap_or(0)   // 0
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::RESULT),
                   PCHECK_DEF("default", false));
    PCHECK_CHECK(params);

    const auto *self = (const Result *) argv[0];

    return HOObject(self->ok ? self->value : argv[1]);
}

constexpr FunctionDef result_methods[] = {
    result_ok,
    result_error,

    result_unwrap,
    result_unwrap_err,
    result_unwrap_or,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::ResultTypeSetup(TypeInfo *self) {
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = ResultEqual;
    ops.to_bool = ResultToBool;
    ops.to_string = ResultToString;

    return TIPropertyAdd(self, result_methods, PropertyFlag::IS_PUBLIC);
}

HOType orbiter::datatype::ResultTypeInit(Isolate *isolate) {
    auto result = MakeType(isolate, "Result",
                           InstanceType::RESULT,
                           (sizeof(Result) - sizeof(OObject)) - sizeof(void *),
                           5,
                           1);
    return result;
}

HResult orbiter::datatype::ResultNew(Isolate *isolate, OObject *object, const bool ok) {
    const auto result = MakeObject<Result>(isolate, InstanceType::RESULT);
    if (result != nullptr) {
        result->value = object;
        result->ok = ok;
    }

    O_GC_TRACK_RETURN(isolate, result, true);
}
