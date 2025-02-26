// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/type.h>

using namespace orbiter::datatype;

RUNTIME_METHOD(type_hash, hash,
               "Return hash value of an object if it has one.",
               0) {
    // TODO: IMPL
    return nullptr;
}

RUNTIME_METHOD(type_id, id,
               "Returns a unique integer identifier for an object. "
               "This identifier remains constant throughout the object's lifetime.",
               0) {
    // TODO: IMPL
    return nullptr;
}

RUNTIME_METHOD(type_repr, repr,
               "Return a string containing a printable representation of an object.",
               0) {
    // TODO: IMPL
    return nullptr;
}

RUNTIME_METHOD(type_str, str, "Return a string version of an object.",
               0) {
    // TODO: IMPL
    return nullptr;
}

const FunctionDef type_methods[] = {
    type_hash,
    type_id,
    type_str,
    type_repr,
    FUNCTIONDEF_SENTINEL
};

HOType orbiter::datatype::TypeInit(Isolate *isolate) {
    auto type = MakeType(isolate, nullptr, InstanceType::TYPE, 0, 4, 0);
    return type;
}

bool orbiter::datatype::TypeSetup(TypeInfo *self) {
    assert(self != nullptr);

    return TIPropertyAdd(self, type_methods);
}
