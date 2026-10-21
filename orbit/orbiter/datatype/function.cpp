// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cctype>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/support/byteops.h>

#include <orbit/orbiter/datatype/function.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

FuncShared *FunSharedNew(orbiter::Isolate *isolate, ORString *name, ORString *doc, const U16 arity,
                         const FunctionPtr func, const FunctionKind kind) {
    orbiter::memory::IsolateAllocator allocator(isolate);
    auto *shared = allocator.calloc<FuncShared>(sizeof(FuncShared));
    if (shared != nullptr) {
        new(&shared->refs) std::atomic_uint(1);

        shared->name = name;
        shared->doc = doc;

        shared->func = func;

        shared->arity = arity;
        shared->kind = kind;
    }

    return shared;
}

FuncShared *FunSharedNew(orbiter::Isolate *isolate, const FuncShared *origin) {
    orbiter::memory::IsolateAllocator allocator(isolate);

    auto *shared = allocator.alloc<FuncShared>(sizeof(FuncShared));
    if (shared != nullptr) {
        stratum::util::MemoryCopy(shared, origin, sizeof(FuncShared));

        new(&shared->refs) std::atomic_uint(1);
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

    orbiter::memory::IsolateAllocator(isolate).free(shared);
}

/**
 * @brief Wrap a FuncShared in a new, GC-tracked Function object.
 *
 * Takes over the caller's reference to `shared`: it moves into the Function, or is released on failure.
 *
 * @param isolate Pointer to the Isolate used to allocate the function.
 * @param shared Shared function data, with a reference owned by the caller.
 * @param closure Captured variables, or nullptr.
 * @param currying Values for partial application, or nullptr.
 *
 * @return Handle to the new Function, or an empty handle on failure.
 */
HFunction MakeFunction(orbiter::Isolate *isolate, FuncShared *shared, Closure *closure, Tuple *currying) {
    auto *fn = MakeObject<Function>(isolate, InstanceType::FUNCTION);
    if (fn != nullptr) {
        fn->shared = shared;

        fn->closure = closure;
        fn->currying = currying;

        O_GC_TRACK_RETURN(isolate, fn, true);
    }

    FunSharedDel(isolate, shared);

    return {};
}

bool FunctionDtor(const Function *self) {
    FunSharedDel(O_GET_ISOLATE(self), self->shared);

    return true;
}

void FunctionTrace(const Function *self, const GCTraceCallback callback, const MSize epoch) {
    callback((OObject *) self, (OObject *) self->closure, epoch);
    callback((OObject *) self, (OObject *) self->currying, epoch);

    // Trace shared
    const auto *shared = self->shared;

    callback((OObject *) self, (OObject *) shared->context, epoch);
    callback((OObject *) self, (OObject *) shared->module, epoch);
    callback((OObject *) self, (OObject *) shared->defaults, epoch);
    callback((OObject *) self, (OObject *) shared->owner_type, epoch);
    callback((OObject *) self, (OObject *) shared->name, epoch);
    callback((OObject *) self, (OObject *) shared->doc, epoch);

    if (shared->IsInterpreted())
        callback((OObject *) self, (OObject *) shared->code, epoch);
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

HFunction orbiter::datatype::FunctionNew(Isolate *isolate, Module *module, TypeInfo *owner, const FunctionDef *def) {
    auto kind = FunctionKind::NATIVE;

    if (def->method)
        kind |= FunctionKind::METHOD;

    if (def->varargs)
        kind |= FunctionKind::REST;

    if (def->kwargs)
        kind |= FunctionKind::KWARGS;

    HORString s_name;
    HORString s_doc;
    HTuple defaults;

    if (def->name != nullptr) {
        s_name = ORStringNew(isolate, def->name);
        if (!s_name)
            return {};
    }

    if (def->doc != nullptr) {
        s_doc = ORStringNew(isolate, def->doc);
        if (!s_doc)
            return {};
    }

    if (def->defaults != nullptr) {
        defaults = MakeDefaultTuple(isolate, def->defaults, strlen(def->defaults));
        if (!defaults)
            return {};
    }

    auto *f_shared = FunSharedNew(isolate, s_name.get(), s_doc.get(), def->params, def->func, kind);
    if (f_shared == nullptr)
        return {};

    f_shared->defaults = defaults.get();
    f_shared->module = module;

    if (def->method)
        f_shared->owner_type = owner;

    return MakeFunction(isolate, f_shared, nullptr, nullptr);
}

HFunction orbiter::datatype::FunctionNew(Code *code, Closure *closure, Tuple *defaults, const LoadFuncFlags flags) {
    auto *isolate = O_GET_ISOLATE(code);

    // LoadFuncFlags and FunctionKind are identical except for NPARAMS, which is mapped to NATIVE in FunctionKind.
    // Since we're creating an interpreted function here (not a native one), we can safely mask out and remove this flag.
    const auto fn_kind = (FunctionKind) (flags & ~LoadFuncFlags::NPARAMS);

    auto *f_shared = FunSharedNew(isolate, code->name, code->doc, code->params_count, nullptr, fn_kind);
    if (f_shared == nullptr)
        return {};

    const auto *fiber = Fiber::Current();

    f_shared->context = fiber->context.context;
    f_shared->module = fiber->context.module;
    f_shared->code = code;
    f_shared->defaults = defaults;

    return MakeFunction(isolate, f_shared, closure, nullptr);
}

HFunction orbiter::datatype::FunctionNew(const Function *func, OObject **args, const U16 argc) {
    auto *isolate = O_GET_ISOLATE(func);

    const auto prev_len = func->currying != nullptr ? func->currying->length : 0;
    const auto currying = TupleNew(isolate, prev_len + argc);
    if (!currying)
        return {};

    // prev_len is 0 when there is no previous currying, so the first loop never touches it
    for (U32 i = 0; i < prev_len; i++)
        TupleAppend(currying.get(), func->currying->objects[i]);

    for (auto i = 0; i < argc; i++)
        TupleAppend(currying.get(), args[i]);

    return MakeFunction(isolate, func->shared->GetRef(), func->closure, currying.get());
}

HFunction orbiter::datatype::FunctionNew(const Function *func) {
    auto *isolate = O_GET_ISOLATE(func);

    auto *shared = FunSharedNew(isolate, func->shared);
    if (shared == nullptr)
        return {};

    return MakeFunction(isolate, shared, func->closure, func->currying);
}

HOType orbiter::datatype::FunctionTypeInit(Isolate *isolate) {
    return MakeType(isolate, "Function", InstanceType::FUNCTION, sizeof(Function) - sizeof(OObject), 0, 0);
}
