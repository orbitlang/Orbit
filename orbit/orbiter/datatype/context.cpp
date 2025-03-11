// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/context.h>

using namespace orbiter::datatype;

void ContextTrace(const Context *self, GCTraceCallback callback, MSize epoch) {
    // TODO: Sync?!
    for (const auto *cursor = self->names.iter_begin; cursor != nullptr; cursor = cursor->iter_next) {
        callback((OObject *) cursor->key, epoch);

        callback(cursor->value.value, epoch);
    }
}

bool orbiter::datatype::ContextDefine(Context *context, ORString *name, OObject *value, PropertyFlag flags) {
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

    return context->names.Insert(entry);
}

bool orbiter::datatype::ContextDefine(Context *context, const char *name, OObject *value, PropertyFlag flags) {
    auto oname = ORStringNew(O_GET_ISOLATE(context), name);

    return ContextDefine(context, oname.get(), value, flags);
}

bool orbiter::datatype::ContextLookup(const Context *context, ORString *name, HOObject &out_value,
                                      PropertyDetail *out_detail) {
    CtxHEntry *entry;

    if (!context->names.Lookup(name, &entry)) {
        // TODO: Not found
        return false;
    }

    out_value = Handle(entry->value.value);

    if (out_detail != nullptr)
        *out_detail = entry->value.detail;

    return true;
}

bool orbiter::datatype::ContextLookup(const Context *context, const char *name, HOObject &out_value,
                                      PropertyDetail *out_detail) {
    auto oname = ORStringNew(O_GET_ISOLATE(context), name);

    return ContextLookup(context, oname.get(), out_value, out_detail);
}

bool orbiter::datatype::ContextSet(const Context *context, ORString *name, OObject *value) {
    CtxHEntry *entry;

    if (!context->names.Lookup(name, &entry)) {
        // TODO: Not found
        return false;
    }

    if (entry->value.detail.IsConstant()) {
        // TODO: Is Constant
        return false;
    }

    entry->value.value = value;

    return true;
}

bool orbiter::datatype::ContextSetup(TypeInfo *self) {
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

        O_GC_TRACK_RETURN(isolate, context, true);
    }

    return Handle(context);
}

HOType orbiter::datatype::ContextInit(Isolate *isolate) {
    auto context = MakeType(isolate, InstanceType::CONTEXT, sizeof(Context) - sizeof(OObject), 0, 0);
    return context;
}
