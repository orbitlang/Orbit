// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/function.h>

using namespace orbiter::datatype;

FuncShared *FunSharedNew(orbiter::Isolate *isolate, const char *name, const char *doc,
                         U16 arity, FunctionPtr func, FunctionKind kind) {
    auto s_name = ORStringNew(isolate, name);
    if (!s_name)
        return nullptr;

    Handle<ORString> s_doc;

    if (doc != nullptr) {
        s_doc = ORStringNew(isolate, doc);
        if (!s_doc)
            return nullptr;
    }

    orbiter::memory::IsolateAllocator allocator(isolate);
    auto *shared = allocator.alloc<FuncShared>(sizeof(FuncShared));
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

void FunSharedDel(orbiter::Isolate *isolate, FuncShared *shared) {
    if (shared->refs.fetch_sub(1) > 1)
        return;

    Release(shared->name);
    Release(shared->doc);

    if (shared->IsInterpreted())
        Release(shared->code);

    orbiter::memory::IsolateAllocator(isolate).free(shared);
}

bool orbiter::datatype::FunctionTypeSetup(TypeInfo *self) {
    return true;
}

Function *orbiter::datatype::FunctionNew(Isolate *isolate, const FunctionDef *def) {
    // TODO: FIX THIS!
    auto kind = FunctionKind::NATIVE;

    if (def->method)
        kind |= FunctionKind::METHOD;

    auto *f_shared = FunSharedNew(isolate, def->name, def->doc, def->params, def->func, kind);
    if (f_shared == nullptr)
        return nullptr;

    auto *fn = MakeObject<Function>(isolate, InstanceType::FUNCTION);

    if (fn != nullptr) {
        fn->shared = f_shared;

        return fn;
    }

    FunSharedDel(isolate, f_shared);

    return nullptr;
}

HOType orbiter::datatype::FunctionTypeInit(Isolate *isolate) {
    auto func = MakeType(isolate, InstanceType::FUNCTION, sizeof(Function) - sizeof(OObject), 0, 0);
    return func;
}
