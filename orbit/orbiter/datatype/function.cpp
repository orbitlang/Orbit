// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/support/byteops.h>

#include <orbit/orbiter/datatype/function.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

FuncShared *FunSharedNew(orbiter::Isolate *isolate, const char *name, const char *doc,
                         const U16 arity, const FunctionPtr func, const FunctionKind kind) {
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
        new(&shared->refs) std::atomic_uint(1);

        shared->name = s_name.release();
        shared->doc = s_doc.release();

        shared->func = func;

        shared->arity = arity;
        shared->kind = kind;
    }

    return shared;
}

HTuple MakeDefaultTuple(orbiter::Isolate *isolate, const char *defaults, const MSize length) {
    const auto comma_count = support::Count((const unsigned char *) defaults, length, (const unsigned char *) ",", 1);

    auto tuple = TupleNew(isolate, (comma_count + 1) * 2);
    if (!tuple)
        return {};

    const char *p = defaults;
    const char *end = defaults + length;
    while (p < end) {
        auto comma = (const char *) memchr(p, ',', end - p);
        const char *tok_end = comma ? comma : end;

        // Trim leading/trailing whitespace
        while (p < tok_end && std::isspace((unsigned char) *p))
            p++;

        while (tok_end > p && std::isspace((unsigned char) tok_end[-1]))
            tok_end--;

        if (tok_end > p) {
            auto str = ORStringNew(isolate, p, (MSize) (tok_end - p));
            if (!str)
                return {};

            if (!TupleAppend(tuple.get(), (OObject *) str.get()))
                return {};

            if (!TupleAppend(tuple.get(), (OObject *) kOddBallSentinel))
                return {};
        }

        p = comma ? comma + 1 : end;
    }

    return tuple;
}

void FunSharedDel(orbiter::Isolate *isolate, FuncShared *shared) {
    if (shared == nullptr || shared->refs.fetch_sub(1) > 1)
        return;

    O_FAST_DECREF(shared->context);
    O_FAST_DECREF(shared->module);

    O_FAST_DECREF(shared->defaults);
    O_FAST_DECREF(shared->owner_type);

    O_FAST_DECREF(shared->name);
    O_FAST_DECREF(shared->doc);

    if (shared->IsInterpreted())
        O_FAST_DECREF(shared->code);

    orbiter::memory::IsolateAllocator(isolate).free(shared);
}

bool FunctionDtor(const Function *self) {
    FunSharedDel(O_GET_ISOLATE(self), self->shared);

    return true;
}

void FunctionTrace(const Function *self, const GCTraceCallback callback, const MSize epoch) {
    callback((OObject *) self->closure, epoch);
    callback((OObject *) self->currying, epoch);
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::FunctionTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) FunctionDtor;
    self->trace = (TraceFn) FunctionTrace;

    return true;
}

HFunction orbiter::datatype::FunctionFromDef(const TypeInfo *type, const FunctionDef &def) {
    for (auto i = 0; i < type->properties.count; i++) {
        auto *property = type->properties.p_array + i;

        if (O_IS_OBJECT(property->value)
            && O_IS_TYPE(property->value, InstanceType::FUNCTION)
            && ((Function *) property->value)->shared->func == def.func)
            return HFunction((Function *) property->value);
    }

    return {};
}

HFunction orbiter::datatype::FunctionNew(Isolate *isolate, TypeInfo *owner, const FunctionDef *def) {
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

    if (def->defaults != nullptr) {
        f_shared->defaults = MakeDefaultTuple(isolate, def->defaults, strlen(def->defaults)).release();
        if (f_shared->defaults == nullptr) {
            FunSharedDel(isolate, f_shared);

            return {};
        }
    }

    if (def->method)
        f_shared->owner_type = O_INCREF(owner);

    auto *fn = MakeObject<Function>(isolate, InstanceType::FUNCTION);
    if (fn != nullptr) {
        fn->shared = f_shared;

        fn->closure = nullptr;
        fn->currying = nullptr;

        O_GC_TRACK_RETURN(isolate, fn, true);
    }

    FunSharedDel(isolate, f_shared);

    return {};
}

HFunction orbiter::datatype::FunctionNew(Code *code, Closure *closure, Tuple *defaults, const LoadFuncFlags flags) {
    auto *isolate = O_GET_ISOLATE(code);

    // LoadFuncFlags and FunctionKind are identical except for NPARAMS, which is mapped to NATIVE in FunctionKind.
    // Since we're creating an interpreted function here (not a native one), we can safely mask out and remove this flag.
    const auto fn_kind = (FunctionKind) (flags & ~LoadFuncFlags::NPARAMS);

    auto *f_shared = FunSharedNew(isolate, nullptr, nullptr, code->params_count, nullptr, fn_kind);
    if (f_shared == nullptr)
        return {};

    f_shared->doc = O_FAST_INCREF(code->doc);
    f_shared->code = O_FAST_INCREF(code);
    f_shared->defaults = O_INCREF(defaults);

    auto *fn = MakeObject<Function>(isolate, InstanceType::FUNCTION);
    if (fn != nullptr) {
        const auto fiber = Fiber::Current();

        fn->shared = f_shared;
        fn->shared->context = O_FAST_INCREF(fiber->context.context);
        fn->shared->module = O_INCREF(fiber->context.module);

        fn->closure = closure;
        fn->currying = nullptr;

        O_GC_TRACK_RETURN(isolate, fn, true);
    }

    FunSharedDel(isolate, f_shared);

    return {};
}

HFunction orbiter::datatype::FunctionNew(const Function *func, OObject **args, const U16 argc) {
    auto *isolate = O_GET_ISOLATE(func);

    const auto prev_len = func->currying != nullptr ? func->currying->length : 0;
    const auto currying = TupleNew(isolate, prev_len + argc);
    if (!currying)
        return {};

    const auto r_curring = currying.get();

    if (func->currying != nullptr) {
        // To make the linter happy :)
        for (auto i = 0; i < prev_len; i++)
            TupleAppend(r_curring, func->currying->objects[i]);
    }

    for (auto i = 0; i < argc; i++)
        TupleAppend(r_curring, args[i]);

    auto *fn = MakeObject<Function>(isolate, InstanceType::FUNCTION);
    if (fn != nullptr) {
        fn->shared = func->shared->GetRef();

        fn->closure = func->closure;
        fn->currying = currying.get();

        O_GC_TRACK_RETURN(isolate, fn, true);
    }

    return {};
}

HOType orbiter::datatype::FunctionTypeInit(Isolate *isolate) {
    auto func = MakeType(isolate, "Function", InstanceType::FUNCTION, sizeof(Function) - sizeof(OObject), 0, 0);
    return func;
}
