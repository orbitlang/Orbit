// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/type.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

HOType orbiter::datatype::TypeInit(Isolate *isolate) {
    auto type = MakeType(isolate, nullptr, "Type", InstanceType::TYPE, 0, 0, 0);
    return type;
}

bool orbiter::datatype::TypeSetup(TypeInfo *self) {
    assert(self != nullptr);

    return true;
}
