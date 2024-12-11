// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cstdlib>

#include <orbit/orbiter/datatype/decimal.h>

using namespace orbiter::datatype;

bool orbiter::datatype::DecimalTypeSetup(Isolate *isolate, TypeInfo *self) {
    return true;
}

HDecimal orbiter::datatype::DecimalNew(Isolate *isolate, DecimalUnderlying number) {
    auto *decimal = MakeObject<Decimal>(isolate, InstanceType::DECIMAL);
    if (decimal != nullptr)
        decimal->value = number;

    return HDecimal(decimal);
}

HDecimal orbiter::datatype::DecimalNew(Isolate *isolate, const char *string) {
    auto *decimal = MakeObject<Decimal>(isolate, InstanceType::DECIMAL);
    if (decimal != nullptr)
        decimal->value = std::strtold(string, nullptr);

    return HDecimal(decimal);
}

TypeInfo *orbiter::datatype::DecimalTypeInit(Isolate *isolate) {
    auto *decimal = MakeType(isolate, InstanceType::DECIMAL, 0, 0, 0);
    return decimal;
}
