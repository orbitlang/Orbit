// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/code.h>

using namespace orbiter::datatype;

bool orbiter::datatype::CodeTypeSetup(Isolate *isolate, TypeInfo *self) {
    return true;
}

TypeInfo *orbiter::datatype::CodeTypeInit(Isolate *isolate) {
    auto *code = MakeType(isolate, InstanceType::CODE, sizeof(Code) - sizeof(OObject), 0, 0);
    return code;
}
