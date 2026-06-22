// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/compiler.h>

#include <orbit/orbiter/fiber.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/byteview.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/oobject.h>
#include <orbit/orbiter/datatype/pcheck.h>

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

RUNTIME_FUNCTION(builtin_eval, eval,
                 R"DOC(
@brief Compile and asynchronously evaluate Orbit source code at runtime.

Compiles src and schedules it for execution on a new fiber, returning a
`Future` immediately. The caller is expected to `await` the future to obtain
the evaluated result and to observe any error raised during evaluation.

When module is false, the code runs in the **global context** — the namespace
shared by the whole program. Any variable or function it declares is installed
there and becomes visible to every other point in the code, exactly as if it
had been written inline.

When module is true, the code runs as a **Module**: its declarations live in
that module's own top-level scope and are reachable from the outside only as
`module.<name>` (and only if declared public). A module can still read freely
from the global context.

By default that shared namespace is the caller's current context. Pass an
explicit `context` to redirect evaluation into a namespace you control — for
instance a `Context(clone=true)` snapshot, to run code against a copy of the
current scope without mutating it, or a fresh `Context()` for an isolated
sandbox.

Compilation happens synchronously inside this call: a syntax or compile error
is reported before any future is returned. Evaluation, by contrast, is
deferred to the scheduler and surfaces through the returned future.

@param name        Name used to identify the unit in diagnostics, and as the
                   module name when module is true.
@param src         Source code to compile, as Bytes or String.
@param context?    Namespace to evaluate in. With module false this is where the
                   code's declarations are installed; with module true it is the
                   global context the module reads from. Defaults to the caller's
                   current context.
@param optim=0     Optimization level, 0 (off) to 3 (aggressive). Out-of-range
                   values are clamped. Defaults to 0, since eval'd code is
                   usually short-lived and compile speed matters more than
                   peak throughput.
@param module=true When true, run the code as a Module with its own scope
                   (declarations reachable as `module.<name>`). When false, run
                   it in the shared global context, where its declarations
                   become globally visible.

@return A Future that resolves to the result of evaluating src.

@panic SyntaxError  When cannot be compiled.
@panic SchedulerError  When the fiber queue is full and evaluation cannot be scheduled.
@panic OOMError     When memory allocation fails.

@example
    let f = eval("<repl>", b"1 + 2")
    await f                              // 3

    # Evaluate as an isolated module.
    let m = await eval("plugin", src, module=true)

    # Evaluate against a private snapshot of the current scope.
    let sandbox = Context(clone=true)
    await eval("sandboxed", src, context=sandbox)
)DOC", 2, "context, optim, module", false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("name", false, InstanceType::STRING),
                   PCHECK_DEF("src", false, InstanceType::BYTES, InstanceType::STRING),
                   PCHECK_DEF("context", true, InstanceType::CONTEXT),
                   PCHECK_DEF("optim", true, InstanceType::NUMBER),
                   PCHECK_DEF("module", true, InstanceType::BOOLEAN));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);
    auto *machine = orbiter::Orbiter::GetInstance();
    const auto *fiber = orbiter::Fiber::Current();

    assert(fiber != nullptr);

    auto *name = (ORString *) argv[0];

    const ByteView source(isolate, argv[1]);
    if (!source)
        return {};

    auto *context = fiber->context.context;
    if (!O_IS_SENTINEL(argv[2]))
        context = (Context *) argv[2];

    IntegerUnderlying optim = 0;
    if (!O_IS_SENTINEL(argv[3])) {
        NumberExtract(argv[3], optim);

        // Clamp into the valid OptimizationLevel range — the value comes straight
        // from user code and feeds an enum cast.
        if (optim < (IntegerUnderlying) liftoff::OptimizationLevel::OFF)
            optim = (IntegerUnderlying) liftoff::OptimizationLevel::OFF;
        else if (optim > (IntegerUnderlying) liftoff::OptimizationLevel::HARD)
            optim = (IntegerUnderlying) liftoff::OptimizationLevel::HARD;
    }

    const auto is_module = O_IS_SENTINEL(argv[4]) ? true : OBOOL_TO_BOOL(argv[4]);

    liftoff::scanner::Scanner scanner(isolate, (const char *) source.Data(), source.Size());

    liftoff::Compiler compiler(isolate, (liftoff::OptimizationLevel) optim, is_module);

    const auto code = compiler.Compile(ORSTRING_TO_CSTR(name), scanner);
    if (!code)
        return {};

    HModule module;

    if (is_module) {
        const auto module_type = ModuleTypeNew(code.get(), name);
        if (!module_type)
            return {};

        module = ModuleNew(module_type.get());
        if (!module)
            return {};
    }

    const auto future = machine->EvalAsync(context, module.get(), code.get());
    return HOObject((OObject *) future.get());
}

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
    ORBIT_MODULE_EXPORT_ALIAS("Context", nullptr),
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

    ORBIT_MODULE_EXPORT_FUNCTION(builtin_eval),
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
