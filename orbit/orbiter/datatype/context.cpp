// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/context.h>

using namespace orbiter::datatype;

bool orbiter::datatype::ContextDefine(Context *context, ORString *name, OObject *value, PropertyFlag flags) {
    CtxHEntry *entry;

    context->names.Lookup(name, &entry);

    if (entry != nullptr) {
        entry->value.value = Handle(O_VFY_INCREF(value));
        entry->value.detail = flags;
        return true;
    }

    entry = context->names.AllocHEntry();
    if (entry == nullptr)
        return false;

    entry->key = O_INCREF(name);
    entry->value.value = Handle(O_VFY_INCREF(value));
    entry->value.detail = flags;

    if (!context->names.Insert(entry)) {
        Release(entry->key);
        entry->value.value.reset();
        return false;
    }

    return true;
}

bool orbiter::datatype::ContextDefine(Context *context, const char *name, OObject *value, PropertyFlag flags) {
    auto oname = ORStringNew(O_GET_ISOLATE(context), name);

    return ContextDefine(context, oname.get(), value, flags);
}

bool orbiter::datatype::ContextLookup(const Context *context, ORString *name, OObject **out_value,
                                      PropertyDetail *out_detail) {
    CtxHEntry *entry;

    if (!context->names.Lookup(name, &entry)) {
        // TODO: Not found
        return false;
    }

    *out_value = entry->value.value.get_inc();

    if (out_detail != nullptr)
        *out_detail = entry->value.detail;

    return true;
}

bool orbiter::datatype::ContextLookup(const Context *context, const char *name, OObject **out_value,
                                      PropertyDetail *out_detail) {
    auto oname = ORStringNew(O_GET_ISOLATE(context), name);

    return ContextLookup(context, oname.get(), out_value, out_detail);
}

bool orbiter::datatype::ContextSet(Context *context, ORString *name, OObject *value) {
    CtxHEntry *entry;

    if (!context->names.Lookup(name, &entry)) {
        // TODO: Not found
        return false;
    }

    if (entry->value.detail.IsConstant()) {
        // TODO: Is Constant
        return false;
    }

    entry->value.value = Handle(O_VFY_INCREF(value));

    return true;
}

bool orbiter::datatype::ContextSetup(TypeInfo *self) {
    return true;
}

HContext orbiter::datatype::ContextNew(Isolate *isolate) {
    auto *context = MakeObject<Context>(isolate, InstanceType::CONTEXT);
    if (context != nullptr) {
        new(&context->names)CtxHMap(isolate);

        if (!context->names.Initialize()) {
            // TODO: Remove release
            Release(context);
            return {};
        }

        O_GC_TRACK_RETURN(isolate, context, true);
    }

    return Handle(context);
}

HOType orbiter::datatype::ContextInit(Isolate *isolate) {
    auto module = MakeType(isolate, InstanceType::CONTEXT, sizeof(Context) - sizeof(OObject), 0, 0);
    return module;
}
