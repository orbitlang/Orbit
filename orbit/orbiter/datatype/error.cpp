// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cerrno>
#include <cstdarg>

#include <orbit/orbiter/fiber.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/errors.h>

#include <orbit/orbiter/datatype/error.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

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
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::ERROR),
                   PCHECK_DEF("kind", false, InstanceType::ATOM)
    );
    PCHECK_CHECK(params);

    const auto *self = (Error *) argv[0];
    return HOObject((OObject *) BOOL_TO_OBOOL(self->kind == (Atom *) argv[1]));
}

RUNTIME_METHOD(error_with_details, with_details,
               R"DOC(
@brief Return a copy of the error with the details field replaced by `details`.

The original error is not mutated.  `kind` and `reason` are shared with the
new object; only the `details` field is swapped.

@param details  The new details value (any type, including nil).

@return A new Error with the same kind and reason but with `details` set to `details`.

@see details, create

@example
    let base = Error.create(@IOError, "read failed")
    let rich = base.with_details({ path: "/tmp/x" })
    rich.details   // { path: "/tmp/x" }
    base.details   // nil  (original unchanged)
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::ERROR),
                   PCHECK_DEF("details", false)
    );
    PCHECK_CHECK(params);

    const auto *self = (Error *) argv[0];

    auto error = ErrorNew(O_GET_ISOLATE(_func), self->kind, self->reason, argv[1]);
    if (!error)
        return {};

    return HOObject(std::move(error));
}

constexpr FunctionDef error_methods[] = {
    error_create,
    error_is,
    error_with_details,

    FUNCTIONDEF_SENTINEL
};

const OPropertyEntry error_props[] = {
    OPROPERTY_ENTRY("kind", 0, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("reason", 1, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("details", 2, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),

    OPROPERTY_SENTINEL
};

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// `str(err)` / `repr(err)`: produces `"<kind>: <reason>"` — the atom's name
/// followed by a colon, a space and the human-readable reason.
static OObject *ErrorToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto *err = (const Error *) self;

    const auto s = ORStringFormat(isolate, "%s: %s",
                                  ORSTRING_TO_CSTR(err->kind->id),
                                  ORSTRING_TO_CSTR(err->reason));
    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::ErrorTypeSetup(TypeInfo *self) {
    // Error stores kind, reason, and details directly in GC-managed slots.
    // The GC automatically traces slot-held references, so no destructor or custom trace callback is needed.
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.to_string = ErrorToString;

    if (!TIPropertyAdd(self, error_props))
        return false;

    const auto ok = TIPropertyAdd(self, error_methods, PropertyFlag::IS_PUBLIC);
    if (ok) {
        const auto ctor = FunctionFromDef(self, error_create);
        if (!ctor)
            return false;

        self->ctor = (OObject *) ctor.get();
    }

    return ok;
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

    O_GC_TRACK_RETURN(isolate, error, true);
}

HError orbiter::datatype::ErrorNew(Isolate *isolate, const char *kind, OObject *details, const char *format, ...) {
    va_list args;

    va_start(args, format);

    return ErrorNewVA(isolate, kind, details, format, args);
}

HOType orbiter::datatype::ErrorTypeInit(Isolate *isolate) {
    auto error = MakeType(isolate, "Error", InstanceType::ERROR, 0, 6, 3);
    return error;
}

int orbiter::datatype::ErrorFormat(const Error *err, char *out, const size_t out_size) noexcept {
    if (out == nullptr || out_size == 0)
        return 0;

    if (err == nullptr) {
        out[0] = '\0';

        return 0;
    }

    // Defensive lookups — we never want this function to itself panic.
    // A malformed Error (missing kind atom, null reason) renders as "?"
    const auto *kind_str = "?";
    if (err->kind != nullptr && err->kind->id != nullptr)
        kind_str = ORSTRING_TO_CSTR(err->kind->id);

    const auto *reason_str = "?";
    int reason_len = 1;

    if (err->reason != nullptr) {
        reason_str = ORSTRING_TO_CSTR(err->reason);
        reason_len = (int) ORSTRING_LENGTH(err->reason);
    }

    return std::snprintf(out, out_size, "%s: %.*s", kind_str, reason_len, reason_str);
}

void orbiter::datatype::ErrorSet(Isolate *isolate, const char *kind, OObject *details, const char *format, ...) {
    va_list args;

    va_start(args, format);

    const auto error = ErrorNewVA(isolate, kind, details, format, args);
    if (!error)
        return;

    Orbiter::RuntimePanic(isolate, (OObject *) error.get());
}

void orbiter::datatype::ErrorSetFromErrno(Isolate *isolate, const char *message) {
    if (message == nullptr)
        message = "";

    // The errno value is attached to every raised OSError as its `details`
    // field (SMI). This lets Orbit-side code dispatch on the raw number
    // (`e.details == EAGAIN`) regardless of which `OSError::Reason` we
    // mapped it to — useful both for unknown errnos (Reason == OTHER) and
    // for the recognised ones when callers want machine-readable detail.
    const auto err = errno;
    auto *errno_smi = (OObject *) O_TO_SMI((MSSize) err);

    switch (err) {
        case ENOENT:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::NOT_FOUND],
                     message);
            break;

        case EACCES:
        case EPERM:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::PERMISSION_DENIED],
                     message);
            break;

        case EEXIST:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::ALREADY_EXISTS],
                     message);
            break;

        case EPIPE:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::BROKEN_PIPE],
                     message);
            break;

        case EINTR:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::INTERRUPTED],
                     message);
            break;

        case EBADF:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::BAD_FD],
                     message);
            break;

        case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::WOULD_BLOCK],
                     message);
            break;

        case ETIMEDOUT:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::TIMEOUT],
                     message);
            break;

        case EINVAL:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::INVALID_ARGUMENT],
                     message);
            break;

        case ENOMEM:
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::NO_MEMORY],
                     message);
            break;

        default:
            // Format: "OS error <num> (<strerror>): <context>" — three %s
            // ordered: %d=err, %s=strerror, %s=message.
            ErrorSet(isolate,
                     OSError::Details[OSError::ID],
                     errno_smi,
                     OSError::Details[OSError::OTHER],
                     err,
                     std::strerror(err),
                     message);
            break;
    }
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
