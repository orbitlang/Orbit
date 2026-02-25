// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cstdarg>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/datatype/error.h>

using namespace orbiter::datatype;

RUNTIME_FUNCTION(error_create, create,
                 R"DOC(
@brief Creates a new error object with the specified atom, message and additional data

Creates an error instance using the provided atom identifier to categorize the error type, a human-readable message string, and optional contextual data.

@param kind Error category identifier atom that defines the error type
@param reason Human-readable error message describing what went wrong
@param details Additional contextual information or details about the error condition

@return New error object

@panic OOMError When insufficient memory is available for error object allocation
@panic ValueError When nil is passed to a non-optional parameter
@panic TypeError When one of the passed parameters has an invalid type
)DOC", 3, false, false) {
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

constexpr FunctionDef error_methods[] = {
    error_create,

    FUNCTIONDEF_SENTINEL
};

const OPropertyEntry error_props[] = {
    OPROPERTY_ENTRY("kind", 0, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("reason", 1, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("details", 2, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),

    OPROPERTY_SENTINEL
};

bool ErrorDtor(Error *self) {
    O_FAST_DECREF(self->kind);
    O_FAST_DECREF(self->reason);
    O_DECREF(self->details);

    return true;
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

bool orbiter::datatype::ErrorTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) ErrorDtor;

    if (!TIPropertyAdd(self, error_props))
        return false;

    return TIPropertyAdd(self, error_methods, PropertyFlag::IS_PUBLIC);
}

HError orbiter::datatype::ErrorNew(Isolate *isolate, Atom *kind, ORString *reason, OObject *details) {
    const auto error = MakeObject<Error>(isolate, InstanceType::ERROR);
    if (error != nullptr) {
        error->kind = O_FAST_INCREF(kind);
        error->reason = O_INCREF(reason);
        error->details = O_INCREF(details);
    }

    O_GC_TRACK_RETURN(isolate, error, false);
}

HError orbiter::datatype::ErrorNew(Isolate *isolate, const char *kind, OObject *details, const char *format, ...) {
    va_list args;

    va_start(args, format);

    return ErrorNewVA(isolate, kind, details, format, args);
}

HOType orbiter::datatype::ErrorTypeInit(Isolate *isolate) {
    auto error = MakeType(isolate, InstanceType::ERROR, 0, 4, 3);
    return error;
}

void orbiter::datatype::ErrorSet(Isolate *isolate, const char *kind, OObject *details, const char *format, ...) {
    va_list args;

    va_start(args, format);

    const auto error = ErrorNewVA(isolate, kind, details, format, args);
    if (!error)
        return;

    const auto fiber = Fiber::Current();
    fiber->Panic((OObject *) error.get());
}
