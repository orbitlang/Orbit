// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/function.h>

using namespace orbiter::datatype;

FuncShared *FunSharedNew(orbiter::Isolate *isolate, const char *name, const char *doc,
                         U16 arity, FunctionPtr func, FunctionKind kind) {
    Handle<ORString> s_name;
    Handle<ORString> s_doc;

    if (name != nullptr) {
        s_name = ORStringNew(isolate, name);
        if (!s_name)
            return nullptr;
    }

    if (doc != nullptr) {
        s_doc = ORStringNew(isolate, doc);
        if (!s_doc)
            return nullptr;
    }

    orbiter::memory::IsolateAllocator allocator(isolate);
    auto *shared = allocator.calloc<FuncShared>(sizeof(FuncShared));
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
    if (shared == nullptr || shared->refs.fetch_sub(1) > 1)
        return;

    O_FAST_DECREF(shared->context);
    O_DECREF(shared->module);

    O_DECREF(shared->defaults);

    O_FAST_DECREF(shared->name);
    O_FAST_DECREF(shared->doc);

    if (shared->IsInterpreted())
        O_FAST_DECREF(shared->code);

    orbiter::memory::IsolateAllocator(isolate).free(shared);
}

bool orbiter::datatype::FunctionTypeSetup(TypeInfo *self) {
    return true;
}

HFunction orbiter::datatype::FunctionNew(Isolate *isolate, const FunctionDef *def) {
    // TODO: FIX THIS!
    auto kind = FunctionKind::NATIVE;

    if (def->method)
        kind |= FunctionKind::METHOD;

    if (def->varargs)
        kind |= FunctionKind::REST;

    if (def->kwargs)
        kind |= FunctionKind::KWARGS;

    auto *f_shared = FunSharedNew(isolate, def->name, def->doc, def->params, def->func, kind);
    if (f_shared == nullptr)
        return {};

    auto *fn = MakeObject<Function>(isolate, InstanceType::FUNCTION);

    if (fn != nullptr) {
        fn->shared = f_shared;

        fn->currying = nullptr;

        O_GC_TRACK_RETURN(isolate, fn, false);
    }

    FunSharedDel(isolate, f_shared);

    return {};
}

HFunction orbiter::datatype::FunctionNew(Code *code, Closure *closure, Tuple *defaults, LoadFuncFlags flags) {
    auto *isolate = O_GET_ISOLATE(code);
    auto fn_kind = FunctionKind::SIMPLE;

    if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::ASYNC))
        fn_kind = FunctionKind::ASYNC;

    if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::INIT))
        fn_kind |= FunctionKind::INIT;

    if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::METHOD))
        fn_kind |= FunctionKind::METHOD;

    if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::REST_PARAMS))
        fn_kind |= FunctionKind::REST;

    auto *f_shared = FunSharedNew(isolate, nullptr, nullptr, code->params_count, nullptr, fn_kind);
    if (f_shared != nullptr) {
        f_shared->doc = O_FAST_INCREF(code->doc);
        f_shared->code = O_FAST_INCREF(code);

        f_shared->defaults = O_INCREF(defaults);
    }

    auto *fn = MakeObject<Function>(isolate, InstanceType::FUNCTION);
    if (fn != nullptr) {
        const auto fiber = Fiber::Current();

        fn->shared = f_shared;
        fn->shared->context = O_FAST_INCREF(fiber->context.context);
        fn->shared->module = O_INCREF(fiber->context.module);

        fn->closure = O_INCREF(closure);
        fn->currying = nullptr;

        O_GC_TRACK_RETURN(isolate, fn, false);
    }

    FunSharedDel(isolate, f_shared);

    return {};
}

HFunction orbiter::datatype::FunctionNew(const Function *func, OObject **args, U16 argc) {
    auto *isolate = O_GET_ISOLATE(func);

    auto currying = TupleNew(isolate, argc);

    if (!currying)
        return {};

    const auto r_curring = currying.get();
    for (auto i = 0; i < argc; i++)
        TupleAppend(r_curring, args[i]);

    auto *fn = MakeObject<Function>(isolate, InstanceType::FUNCTION);
    if (fn != nullptr) {
        fn->shared = func->shared;

        fn->shared->refs.fetch_add(1);

        fn->currying = currying.release();

        O_GC_TRACK_RETURN(isolate, fn, false);
    }

    return {};
}

HOType orbiter::datatype::FunctionTypeInit(Isolate *isolate) {
    auto func = MakeType(isolate, InstanceType::FUNCTION, sizeof(Function) - sizeof(OObject), 0, 0);
    return func;
}
