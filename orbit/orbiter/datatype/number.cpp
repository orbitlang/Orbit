// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/number.h>

using namespace orbiter::datatype;

bool orbiter::datatype::NumberTypeSetup(Context *ctx, TypeInfo *self) {
    return true;
}

HNumber orbiter::datatype::IntNew(const Context *ctx, IntegerUnderlying value) {
    auto inline_max = 0x1ULL << ((sizeof(MSize) * 8) - 1);

    if (value < inline_max)
        return HNumber((Number *) ((value << 1) | 0x1));

    auto *num = MakeObject<Number>(ctx, InstanceType::NUMBER);
    if (num == nullptr)
        return {};

    num->sint = value;

    return HNumber(num);
}

HNumber orbiter::datatype::IntNew(const Context *ctx, const char *string, int base) {
    const auto num = std::strtol(string, nullptr, base);

    return IntNew(ctx, num);
}

HNumber orbiter::datatype::UIntNew(const Context *ctx, UIntegerUnderlying value) {
    auto *num = MakeObject<Number>(ctx, InstanceType::NUMBER);
    if (num == nullptr)
        return {};

    num->uint = value;

    return HNumber(num);
}

HNumber orbiter::datatype::UIntNew(const Context *ctx, const char *string, int base) {
    const auto num = std::strtoul(string, nullptr, base);

    return UIntNew(ctx, num);
}

TypeInfo *orbiter::datatype::NumberTypeInit(Context *ctx) {
    auto *number = MakeType(InstanceType::NUMBER, 0, 0, 0);
    return number;
}
