// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cstdlib>

#include <orbit/orbiter/datatype/number.h>

using namespace orbiter::datatype;

bool orbiter::datatype::NumberTypeSetup(TypeInfo *self) {
    return true;
}

HNumber orbiter::datatype::IntNew(Isolate *isolate, const IntegerUnderlying value) {
    if (value < kSMIMaxSize)
        return HNumber((Number *) ((value << 1) | 0x1));

    auto *num = MakeObject<Number>(isolate, InstanceType::NUMBER);
    if (num == nullptr)
        return {};

    num->sint = value;

    O_GC_TRACK_RETURN(isolate, num, false);
}

HNumber orbiter::datatype::IntNew(Isolate *isolate, const char *string, int base) {
    const auto num = std::strtol(string, nullptr, base);

    return IntNew(isolate, num);
}

HNumber orbiter::datatype::SmiNeg(Isolate *isolate, OObject *value) noexcept {
    const auto n = (MSSize) ((MSize) value >> 1);

    if (n == kSMIMinSize) [[unlikely]] {
        // IntNew cannot be used here: its unsigned range check incorrectly folds
        // values >= 2^62 (64bit machine) back into a corrupt SMI.
        auto *num = MakeObject<Number>(isolate, InstanceType::NUMBER);
        if (num == nullptr)
            return {};

        num->sint = -(IntegerUnderlying) n;

        O_GC_TRACK_RETURN(isolate, num, false);
    }

    return HNumber((Number *) (((-n) << 1) | 0x01));
}

HNumber orbiter::datatype::UIntNew(Isolate *isolate, const UIntegerUnderlying value) {
    auto *num = MakeObject<Number>(isolate, InstanceType::NUMBER);
    if (num == nullptr)
        return {};

    num->uint = value;

    O_GC_TRACK_RETURN(isolate, num, false);
}

HNumber orbiter::datatype::UIntNew(Isolate *isolate, const char *string, int base) {
    const auto num = std::strtoul(string, nullptr, base);

    return UIntNew(isolate, num);
}

HOType orbiter::datatype::NumberTypeInit(Isolate *isolate) {
    auto number = MakeType(isolate, InstanceType::NUMBER, sizeof(Number) - sizeof(OObject), 0, 0);
    return number;
}
