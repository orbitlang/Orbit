// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <shared_mutex>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/pcheck.h>

#include <orbit/orbiter/datatype/context.h>

using namespace orbiter::datatype;

bool ContextDtor(Context *self) {
    self->names.Finalize(nullptr);
    self->names.~CtxHMap();

    self->lock.~AsyncRWLock();

    return true;
}

void ContextTrace(const Context *self, const GCTraceCallback callback, const MSize epoch) {
    for (const auto *cursor = self->names.iter_begin; cursor != nullptr; cursor = cursor->iter_next) {
        callback((OObject *) cursor->key, epoch);

        callback(cursor->value.value, epoch);
    }
}

/// Two contexts are equal when they bind the same set of names to equal values.
/// Like Dict, only the values are compared (via the generic Equal() dispatch);
/// the per-binding detail flags (const/public/weak) are not considered.
static bool ContextEqual(const OObject *left, const OObject *right) {
    if (left == right)
        return true;

    if (!O_IS_OBJECT(right) || !O_IS_TYPE(right, InstanceType::CONTEXT))
        return false;

    auto *a = (Context *) left;
    auto *b = (Context *) right;

    std::shared_lock la(a->lock);
    std::shared_lock lb(b->lock);

    if (a->names.length != b->names.length)
        return false;

    for (const auto *cursor = a->names.iter_begin; cursor != nullptr; cursor = cursor->iter_next) {
        CtxHEntry *entry;

        if (b->names.Lookup(cursor->key, &entry) != LookupResult::OK)
            return false;

        if (!Equal(cursor->value.value, entry->value.value))
            return false;
    }

    return true;
}

bool orbiter::datatype::ContextDefine(Context *context, ORString *name, OObject *value, const PropertyFlag flags) {
    std::unique_lock _(context->lock);

    CtxHEntry *entry;

    context->names.Lookup(name, &entry);

    if (entry != nullptr) {
        entry->value.value = value;
        entry->value.detail = flags;

        return true;
    }

    entry = context->names.AllocHEntry();
    if (entry == nullptr)
        return false;

    entry->key = name;
    entry->value.value = value;
    entry->value.detail = flags;

    if (context->names.Insert(entry) != LookupResult::OK) {
        context->names.FreeHEntry(entry);

        return false;
    }

    return true;
}

bool orbiter::datatype::ContextDefine(Context *context, const char *name, OObject *value, const PropertyFlag flags) {
    auto oname = ORStringNew(O_GET_ISOLATE(context), name);

    if (oname)
        return ContextDefine(context, oname.get(), value, flags);

    return false;
}

bool orbiter::datatype::ContextImportFromModule(Context *context, const Module *module) {
    if (!O_IS_TYPE(module, InstanceType::MODULE))
        return false;

    const auto mod_type = O_GET_TYPE(module);

    for (auto i = 0; i < mod_type->properties.count; i++) {
        const auto *property = mod_type->properties.p_array + i;
        bool ok = false;

        const auto *name = ORSTRING_TO_CSTR(property->name);
        const auto length = ORSTRING_LENGTH(property->name);

        // Simple filter to avoid importing dunder names (e.g., __name__, __doc__, etc.)
        if (length > 3 && name[0] == '_' && name[1] == '_' && name[length - 1] == '_' && name[length - 2] == '_')
            continue;

        if (ENUMBITMASK_ISTRUE(property->detail, PropertyFlag::IS_PUBLIC)) {
            if (ENUMBITMASK_ISTRUE(property->detail, PropertyFlag::IN_OBJECT)) {
                auto **slot = O_SLOT(module, mod_type);

                ok = ContextDefine(context, property->name, slot[property->slot], PropertyFlag::IS_PUBLIC);
            } else
                ok = ContextDefine(context, property->name, property->value, PropertyFlag::IS_PUBLIC);
        }

        if (!ok)
            return false;
    }

    return true;
}

bool orbiter::datatype::ContextLookup(Context *context, ORString *name, HOObject &out_value,
                                      PropertyDetail *out_detail) {
    std::shared_lock _(context->lock);

    CtxHEntry *entry;
    switch (context->names.Lookup(name, &entry)) {
        case LookupResult::OK:
            break;
        case LookupResult::NOT_FOUND:
            ErrorSet(O_GET_ISOLATE(context),
                     NameError::Details[NameError::Reason::ID],
                     nullptr,
                     NameError::Details[NameError::Reason::NOT_DEFINED],
                     ORSTRING_TO_CSTR(name));

            return false;
        case LookupResult::ERROR:
            return false;
    }

    out_value = Handle(entry->value.value);

    if (out_detail != nullptr)
        *out_detail = entry->value.detail;

    return true;
}

bool orbiter::datatype::ContextLookup(Context *context, const char *name, HOObject &out_value,
                                      PropertyDetail *out_detail) {
    auto oname = ORStringNew(O_GET_ISOLATE(context), name);
    if (oname)
        return ContextLookup(context, oname.get(), out_value, out_detail);

    return false;
}

bool orbiter::datatype::ContextSet(Context *context, ORString *name, OObject *value) {
    std::unique_lock _(context->lock);

    CtxHEntry *entry;
    switch (context->names.Lookup(name, &entry)) {
        case LookupResult::OK:
            break;
        case LookupResult::NOT_FOUND:
            ErrorSet(O_GET_ISOLATE(context),
                     NameError::Details[NameError::Reason::ID],
                     nullptr,
                     NameError::Details[NameError::Reason::NOT_DEFINED],
                     ORSTRING_TO_CSTR(name));

            return false;
        case LookupResult::ERROR:
            return false;
    }

    if (entry->value.detail.IsConstant()) {
        ErrorSet(O_GET_ISOLATE(context),
                 AttributeError::Details[AttributeError::Reason::ID],
                 nullptr,
                 AttributeError::Details[AttributeError::Reason::CONSTANT_ASSIGN],
                 ORSTRING_TO_CSTR(name));

        return false;
    }

    entry->value.value = value;

    return true;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_FUNCTION(context_create, create,
                 R"DOC(
@brief Create a new execution context.

By default the context is empty. When `clone` is true, the new context is
seeded with a snapshot of the calling fiber's current context: every name
currently in scope is copied over (the values are shared, not deep-copied,
and each binding keeps its const/public flags).

A Context can be handed to `eval` to run code in a custom namespace.

@param clone?  When true, copy the current context's bindings into the new
               context. Defaults to false (empty context).

@return A new Context instance.

@see eval

@example
    let empty = Context()              // empty namespace
    let snap  = Context(clone=true)    // copy of the current scope
)DOC", 0, "clone", false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("clone", true, InstanceType::BOOLEAN));
    PCHECK_CHECK(params);

    auto *isolate = O_GET_ISOLATE(_func);

    const auto context = ContextNew(isolate);
    if (!context)
        return {};

    if (!O_IS_SENTINEL(argv[0]) && OBOOL_TO_BOOL(argv[0])) {
        auto *current = orbiter::Fiber::Current()->context.context;
        if (current != nullptr) {
            std::shared_lock _(current->lock);

            for (const auto *cursor = current->names.iter_begin; cursor != nullptr; cursor = cursor->iter_next) {
                if (!ContextDefine(context.get(), cursor->key, cursor->value.value, cursor->value.detail.flags))
                    return {};
            }
        }
    }

    return HOObject((OObject *) context.get());
}

constexpr FunctionDef context_methods[] = {
    context_create,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::ContextSetup(TypeInfo *self) {
    self->dtor = (DtorFn) ContextDtor;
    self->trace = (TraceFn) ContextTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = ContextEqual;

    if (!TIPropertyAdd(self, context_methods, PropertyFlag::IS_PUBLIC))
        return false;

    const auto ctor = FunctionFromDef(self, context_create);
    if (!ctor)
        return false;

    self->ctor = (OObject *) ctor.get();

    return true;
}

HContext orbiter::datatype::ContextNew(Isolate *isolate) {
    auto *context = MakeObject<Context>(isolate, InstanceType::CONTEXT);
    if (context != nullptr) {
        new(&context->names)CtxHMap(isolate);

        if (!context->names.Initialize()) {
            isolate->gc->RawFree((OObject *) context, false);

            return {};
        }

        new(&context->lock)sync::AsyncRWLock();

        O_GC_TRACK_RETURN(isolate, context, true);
    }

    return Handle(context);
}

HOType orbiter::datatype::ContextInit(Isolate *isolate) {
    auto context = MakeType(isolate, "Context", InstanceType::CONTEXT, sizeof(Context) - sizeof(OObject), 1, 0);
    return context;
}
