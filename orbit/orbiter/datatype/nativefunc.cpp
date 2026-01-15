// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/datatype/nativefunc.h>

using namespace orbiter::datatype;

bool orbiter::datatype::NativeFuncTypeSetup(TypeInfo *self) {
    return true;
}

HNativeFunc orbiter::datatype::NativeFuncNew(Isolate *isolate, native::NativeBinding *binding,
                                             native::DLHandle handle) {
    memory::IsolateAllocator allocator(isolate);

    auto *func = MakeObject<NativeFunc>(isolate, InstanceType::NATIVE_FUNC);
    if (func == nullptr)
        return {};

    func->name = O_INCREF(binding->name);
    func->doc = nullptr; // FIXME
    func->handle = handle;
    func->ret_type = binding->ret_type;
    func->arity = binding->params.count;

    func->params = allocator.alloc<native::NativeParam>(func->arity  * sizeof(native::NativeParam));
    if (func->params == nullptr) {
        allocator.free(func->params);

        return {};
    }

    for (auto i = 0; i < func->arity ; ++i) {
        func->params[i].name = O_FAST_INCREF(binding->params.params[i].name);
        func->params[i].type = binding->params.params[i].type;
    }

    O_GC_TRACK_RETURN(isolate, func, false);
}

HOType orbiter::datatype::NativeFuncTypeInit(Isolate *isolate) {
    auto func = MakeType(isolate, InstanceType::NATIVE_FUNC, sizeof(NativeFunc) - sizeof(OObject), 0, 0);
    return func;
}
