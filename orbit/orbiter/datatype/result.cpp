// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/result.h>

using namespace orbiter::datatype;

// TODO methods:
/*
 *result.ok()
 *result.error()
 *
 *result.unwrap()
 *result.unwrap_err()
 *
 *result.unwrap_or(default)
 *
 *
 */

bool orbiter::datatype::ResultTypeSetup(TypeInfo *self) {
    return true;
}

HOType orbiter::datatype::ResultTypeInit(Isolate *isolate) {
    auto type = MakeType(isolate, InstanceType::RESULT, sizeof(Result) - sizeof(OObject), 0, 0);
    return type;
}

HResult orbiter::datatype::ResultNew(Isolate *isolate, OObject *object, const bool ok) {
    const auto result = MakeObject<Result>(isolate, InstanceType::RESULT);
    if (result != nullptr) {
        result->value = object;
        result->ok = ok;
    }

    O_GC_TRACK_RETURN(isolate, result, false);
}
