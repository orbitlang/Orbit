// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cstdarg>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/error.h>

#include <orbit/orbiter/runtime.h>

using namespace orbiter::datatype;

RUNTIME_FUNCTION(error_create, create,
                 R"DOC(
@brief Create a new error object with the specified kind, message, and optional details.

@param kind        Atom that categorises the error type.
@param reason      Human-readable description of what went wrong.
@param details?    Additional context attached to the error (defaults to nil).

@return A new Error object.

@panic TypeError  When a parameter has an invalid type.

@see message, with_details

@example
    Error.create(@IOError, "file not found")
    Error.create(@NIOError, "connection refused", { url: "..." })
)DOC", 2, "details", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("kind", false, InstanceType::ATOM),
                   PCHECK_DEF("reason", false, InstanceType::STRING),
                   PCHECK_DEF("details", true)
    );

    PCHECK_CHECK(params);

    auto error = ErrorNew(O_GET_ISOLATE(*argv), (Atom *) argv[0], (ORString *) argv[1], argv[2]);
    if (!error)
        return {};

    return HOObject(std::move(error));
}

RUNTIME_METHOD(error_is, is,
               R"DOC(
@brief Return true if the error's kind matches the given atom.

Because atoms are interned, the comparison is a fast pointer equality check.

@param kind  The atom to compare against the error's kind.

@return true if the error's kind equals `kind`, false otherwise.

@see kind, message

@example
    let e = Error.create(@IOError, "file not found")
    e.is(@IOError)   // true
    e.is(@NIOError)  // false
)DOC", 2, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("kind", false, InstanceType::ATOM));

    PCHECK_CHECK(params);

    const auto *self = (Error *) argv[0];
    return HOObject((OObject *) BOOL_TO_OBOOL(self->kind == (Atom *) argv[1]));
}

RUNTIME_METHOD(error_message, message,
               R"DOC(
@brief Return a formatted string combining the error kind and reason.

The returned string has the form `"<kind>: <reason>"` — the atom name followed
by a colon, a space, and the human-readable reason message.

@return A String of the form `"<kind>: <reason>"`.

@see str, kind, reason

@example
    let e = Error.create(@IOError, "file not found")
    e.message()   // "IOError: file not found"
)DOC", 1, false, false) {
    const auto *self = (Error *) argv[0];

    auto s = ORStringFormat(O_GET_ISOLATE(_func), "%s: %s",
                            ORSTRING_TO_CSTR(self->kind->id),
                            ORSTRING_TO_CSTR(self->reason));
    if (!s)
        return {};

    return HOObject(std::move(s));
}

RUNTIME_METHOD(error_with_details, with_details,
               R"DOC(
@brief Return a copy of the error with the details field replaced by `v`.

The original error is not mutated.  `kind` and `reason` are shared with the
new object; only the `details` field is swapped.

@param v  The new details value (any type, including nil).

@return A new Error with the same kind and reason but with `details` set to `v`.

@see details, create

@example
    let base = Error.create(@IOError, "read failed")
    let rich = base.with_details({ path: "/tmp/x" })
    rich.details   // { path: "/tmp/x" }
    base.details   // nil  (original unchanged)
)DOC", 2, false, false) {
    const auto *self = (Error *) argv[0];

    auto error = ErrorNew(O_GET_ISOLATE(_func), self->kind, self->reason, argv[1]);
    if (!error)
        return {};

    return HOObject(std::move(error));
}

constexpr FunctionDef error_methods[] = {
    error_create,
    error_is,
    error_message,
    error_with_details,

    FUNCTIONDEF_SENTINEL
};

const OPropertyEntry error_props[] = {
    OPROPERTY_ENTRY("kind", 0, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("reason", 1, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("details", 2, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),

    OPROPERTY_SENTINEL
};

bool orbiter::datatype::ErrorTypeSetup(TypeInfo *self) {
    // Error stores kind, reason, and details directly in GC-managed slots.
    // The GC automatically traces slot-held references, so no destructor or custom trace callback is needed.
    if (!TIPropertyAdd(self, error_props))
        return false;

    return TIPropertyAdd(self, error_methods, PropertyFlag::IS_PUBLIC);
}

HError ErrorNewVA(orbiter::Isolate *isolate, const char *kind, OObject *details, const char *format, va_list args) {
    const auto atom = AtomNew(isolate, kind);
    if (!atom)
        return {};

    const auto reason = ORStringFormat(isolate, format, args);
    va_end(args);

    if (!reason)
        return {};

    return ErrorNew(isolate, atom.get(), reason.get(), details);
}

HError orbiter::datatype::ErrorNew(Isolate *isolate, Atom *kind, ORString *reason, OObject *details) {
    const auto error = MakeObject<Error>(isolate, InstanceType::ERROR);
    if (error != nullptr) {
        error->kind = kind;
        error->reason = reason;
        error->details = details;
    }

    O_GC_TRACK_RETURN(isolate, error, false);
}

HError orbiter::datatype::ErrorNew(Isolate *isolate, const char *kind, OObject *details, const char *format, ...) {
    va_list args;

    va_start(args, format);

    return ErrorNewVA(isolate, kind, details, format, args);
}

HOType orbiter::datatype::ErrorTypeInit(Isolate *isolate) {
    auto error = MakeType(isolate, "Error", InstanceType::ERROR, 0, 7, 3);
    return error;
}

void orbiter::datatype::ErrorSet(Isolate *isolate, const char *kind, OObject *details, const char *format, ...) {
    va_list args;

    va_start(args, format);

    const auto error = ErrorNewVA(isolate, kind, details, format, args);
    if (!error)
        return;

    Orbiter::RuntimePanic(isolate, (OObject *) error.get());
}

void orbiter::datatype::ErrorSetWithObjType(Isolate *isolate, const char *kind, const char *format, const char *p1,
                                            const OObject *target) {
    char error[24];

    GetTypeName(isolate, target, error, sizeof(error));

    if (p1 == nullptr)
        ErrorSet(isolate, kind, nullptr, format, error);
    else
        ErrorSet(isolate, kind, nullptr, format, p1, error);
}
