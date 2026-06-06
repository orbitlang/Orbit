// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/module/modules.h>


using namespace orbiter::datatype;
using namespace orbiter::module;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

/// Module init callback — called once per import by the builtin locator,
/// right after the static `builtin_entries` table has been materialised
/// into the module's TypeInfo. The static entries reserve a slot for each
/// name with a `nullptr` value; here we patch each slot to point at the
/// per-isolate `TypeInfo *` retrieved from `Isolate::primitive[]`.
///
/// This is the only place where the type pointers can be set, because
/// they are runtime objects allocated by `Isolate::New()` — a static
/// initializer in C++ can't reference them.
static bool BuiltinInit(Module *self) {
    const auto *isolate = O_GET_ISOLATE(self);
    const auto *type = O_GET_TYPE(self);

    for (int i = 0; i < kInstanceTypeCount; i++) {
        if (isolate->primitive[i] == nullptr)
            continue;

        auto *prop = TIFindLocalProperty(type, isolate->primitive[i]->name);
        if (prop == nullptr)
            continue;

        auto *target = isolate->primitive[i];
        if (target == nullptr)
            return false;

        // The bulk-pass installed `nullptr` here, so there is no previous
        // refcount to release.  We bump the type's refcount because the
        // module's TypeInfo now holds a strong reference.
        prop->value = O_INCREF((OObject *) target);
    }

    return true;
}

// *********************************************************************************************************************
// PRIMITIVES
// *********************************************************************************************************************

RUNTIME_FUNCTION(builtin_panicking, panicking,
                 R"DOC(
@brief Report whether the current fiber is unwinding a panic.

Returns `true` when the calling fiber is in the middle of a panic
propagation — i.e. an `Error` has been raised and the stack is being
unwound, but no `try` block has caught it yet.  Returns `false` during
normal execution.

The primary use case is inside `defer` blocks, where the same cleanup
code can run on both the happy path and the panic path and may want to
distinguish between the two — for example to skip emitting a "success"
log line, to release a resource differently, or to add extra context to
the in-flight error before it continues unwinding.

The query is scoped to the **current fiber**: a panic in another fiber
does not affect this result.  The value reflects the state at the moment
of the call; once the panic is caught or the fiber exits, subsequent
calls will return `false` again.

@return `true` if a panic is currently propagating in this fiber,
        `false` otherwise.

@example
    func write_log(file, msg) {
        defer file.close()

        defer func () {
            if builtin.panicking() {
                file.write(b"[ABORTED]\n")     // we left mid-write
            } else {
                file.write(b"[OK]\n")          // wrote msg successfully
            }
        }()

        file.write(msg)                        // may raise
    }
)DOC", 0, nullptr, false, false) {
    return HOObject((OObject *) BOOL_TO_OBOOL(orbiter::Fiber::Current()->IsPanicking()));
}

// *********************************************************************************************************************
// MODULE TABLE
// *********************************************************************************************************************

const ModuleEntry builtin_entries[] = {
    ORBIT_MODULE_EXPORT_ALIAS("Atom", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Bytes", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Chan", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Class", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Closure", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Code", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Decimal", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Dict", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Error", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Function", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Future", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Generator", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Iterator", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("List", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Module", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("NativeFunc", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Number", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Rawptr", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Result", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Set", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("String", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Trait", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Tuple", nullptr),
    ORBIT_MODULE_EXPORT_ALIAS("Type", nullptr),

    ORBIT_MODULE_EXPORT_FUNCTION(builtin_panicking),

    ORBIT_MODULE_SENTINEL
};

ModuleInit ModuleBuiltin = {
    "::orbit::builtin",
    "@brief Built-in Orbit types."
    "\n\n"
    "Exposes every primitive Orbit type by name (Atom, Bytes, Decimal, Dict, "
    "Error, List, Set, String, Tuple, Type, ...) so user code can use them "
    "with `is`, `type()` comparisons, and reflective access.",
    "1.0.0",
    builtin_entries,
    BuiltinInit,
    nullptr
};

const ModuleInit *orbiter::module::module_builtin_ = &ModuleBuiltin;
