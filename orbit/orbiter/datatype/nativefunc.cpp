// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/datatype/nativefunc.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Native functions are unique bindings — identity equality only.
static bool NativeFuncEqual(const OObject *left, const OObject *right) {
    return left == right;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A native function object is always callable and therefore always truthy.
static bool NativeFuncToBool(const OObject *) {
    return true;
}

/// Format: "native name(<arity> params) -> ret_type at 0xADDR"
static OObject *NativeFuncToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto *func = (const NativeFunc *) self;

    const auto *name = ORSTRING_TO_CSTR(func->name);
    const auto *ret_name = NativeTypeNames[(int) func->ret_type];

    const auto s = ORStringFormat(isolate, "native %s(%d params): %s at %p",
                                  name, func->arity, ret_name, self);

    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool NativeFuncDtor(const NativeFunc *self) {
    const orbiter::memory::IsolateAllocator allocator(O_GET_ISOLATE(self));

    allocator.free(self->params);

    return true;
}

void NativeFuncTrace(const NativeFunc *self, const GCTraceCallback callback, const MSize epoch) {
    callback((OObject *) self->name, epoch);

    if (self->doc != nullptr)
        callback((OObject *) self->doc, epoch);

    for (auto i = 0; i < self->arity; ++i)
        callback((OObject *) self->params[i].name, epoch);
}

bool orbiter::datatype::NativeFuncTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) NativeFuncDtor;
    self->trace = (TraceFn) NativeFuncTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = NativeFuncEqual;
    ops.to_bool = NativeFuncToBool;
    ops.to_string = NativeFuncToString;
    ops.to_repr = NativeFuncToString;

    return true;
}

HNativeFunc orbiter::datatype::NativeFuncNew(Isolate *isolate, native::NativeBinding *binding,
                                             native::DLHandle handle) {
    memory::IsolateAllocator allocator(isolate);

    auto *func = MakeObject<NativeFunc>(isolate, InstanceType::NATIVE_FUNC);
    if (func == nullptr)
        return {};

    func->name = binding->name;
    func->doc = nullptr; // FIXME
    func->handle = handle;
    func->ret_type = binding->ret_type;
    func->arity = binding->params.count;

    func->params = allocator.alloc<native::NativeParam>(func->arity * sizeof(native::NativeParam));
    if (func->params == nullptr) {
        isolate->gc->RawFree((OObject *) func, false);

        return {};
    }

    for (auto i = 0; i < func->arity; ++i) {
        func->params[i].name = binding->params.params[i].name;
        func->params[i].type = binding->params.params[i].type;
    }

    O_GC_TRACK_RETURN(isolate, func, false);
}

HOType orbiter::datatype::NativeFuncTypeInit(Isolate *isolate) {
    auto func = MakeType(isolate, "NativeFunc", InstanceType::NATIVE_FUNC, sizeof(NativeFunc) - sizeof(OObject), 0, 0);
    return func;
}
