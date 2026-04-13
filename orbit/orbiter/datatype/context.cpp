// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <shared_mutex>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

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

bool orbiter::datatype::ContextDefine(Context *context, ORString *name, OObject *value, PropertyFlag flags) {
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

    if (!context->names.Insert(entry)) {
        context->names.FreeHEntry(entry);

        return false;
    }

    return true;
}

bool orbiter::datatype::ContextDefine(Context *context, const char *name, OObject *value, PropertyFlag flags) {
    auto oname = ORStringNew(O_GET_ISOLATE(context), name);

    if (oname)
        return ContextDefine(context, oname.get(), value, flags);

    return false;
}

bool orbiter::datatype::ContextLookup(Context *context, ORString *name, HOObject &out_value,
                                      PropertyDetail *out_detail) {
    std::shared_lock _(context->lock);

    CtxHEntry *entry;

    if (!context->names.Lookup(name, &entry)) {
        ErrorSet(O_GET_ISOLATE(context),
                 NameError::Details[NameError::Reason::ID],
                 nullptr,
                 NameError::Details[NameError::Reason::NOT_DEFINED],
                 ORSTRING_TO_CSTR(name));

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

    if (!context->names.Lookup(name, &entry)) {
        ErrorSet(O_GET_ISOLATE(context),
                 NameError::Details[NameError::Reason::ID],
                 nullptr,
                 NameError::Details[NameError::Reason::NOT_DEFINED],
                 ORSTRING_TO_CSTR(name));

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

bool orbiter::datatype::ContextSetup(TypeInfo *self) {
    self->dtor = (DtorFn) ContextDtor;
    self->trace = (TraceFn) ContextTrace;

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
    auto context = MakeType(isolate, "Context", InstanceType::CONTEXT, sizeof(Context) - sizeof(OObject), 0, 0);
    return context;
}
