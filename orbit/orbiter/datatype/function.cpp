// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/memory/memory.h>

#include <orbit/orbiter/datatype/function.h>

using namespace orbiter::datatype;

FuncShared *FunSharedNew(const orbiter::Context *ctx, const char *name, const char *doc,
                         U16 arity, FunctionPtr func, FunctionKind kind) {
    auto s_name = ORStringNew(ctx, name);
    if (!s_name)
        return nullptr;

    Handle<ORString> s_doc;

    if (doc != nullptr) {
        s_doc = ORStringNew(ctx, doc);
        if (!s_doc)
            return nullptr;
    }

    auto *shared = (FuncShared *) orbiter::memory::Alloc(sizeof(FuncShared));
    if (shared != nullptr) {
        shared->refs = 1;

        shared->name = s_name.release();
        shared->doc = s_doc.release();

        shared->func = func;

        shared->arity = arity;
        shared->kind = kind;
    }

    return shared;
}

void FunSharedDel(FuncShared *shared) {
    if (shared->refs.fetch_sub(1) > 1)
        return;

    Release(shared->name);
    Release(shared->doc);

    if (shared->IsInterpreted())
        Release(shared->code);

    orbiter::memory::Free(shared);
}

bool orbiter::datatype::FunctionTypeSetup(const Context *ctx, TypeInfo *self) {
    return true;
}

Function *orbiter::datatype::FunctionNew(const Context *ctx, const FunctionDef *def) {
    auto kind = FunctionKind::NATIVE;

    if (def->method)
        kind |= FunctionKind::METHOD;

    auto *f_shared = FunSharedNew(ctx, def->name, def->doc, def->params, def->func, kind);
    if (f_shared == nullptr)
        return nullptr;

    auto *fn = MakeObject<Function>(ctx, InstanceType::FUNCTION);

    if (fn != nullptr) {
        fn->shared = f_shared;

        return fn;
    }

    FunSharedDel(f_shared);

    return nullptr;
}

TypeInfo *orbiter::datatype::FunctionTypeInit(const Context *ctx) {
    auto *func = MakeType(ctx, InstanceType::FUNCTION, sizeof(Function) - sizeof(OObject), 0, 0);
    return func;
}
