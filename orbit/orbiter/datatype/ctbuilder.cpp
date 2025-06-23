// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/ctbuilder.h>

using namespace orbiter::datatype;

bool orbiter::datatype::ClassTypeSetup(TypeInfo *self) {
    return true;
}

bool orbiter::datatype::TraitTypeSetup(TypeInfo *self) {
    return true;
}

HOType orbiter::datatype::ClassTypeInit(Isolate *isolate) {
    auto clazz = MakeType(isolate, InstanceType::CLASS, 0, 0, 0);
    return clazz;
}

HOType orbiter::datatype::ClassTypeNew(const Code *code, TypeInfo *super, TypeInfo **traits, U16 traits_count) {
    const auto isolate = O_GET_ISOLATE(code);
    HOType clazz;

    if (super == nullptr)
        clazz = MakeTypeExtended(isolate, InstanceType::CLASS, 0, code->exported.length, code->slots_count);
    else
        clazz = MakeType(isolate, super, InstanceType::CLASS, 0, code->exported.length, code->slots_count);

    if (!clazz)
        return {};

    for (auto i = 0; i < code->exported.length; i++) {
        const auto *symbol = code->exported.symbols + i;

        PropertyFlag pf{};
        if (ENUMBITMASK_ISTRUE(symbol->flags, VariableFlags::PUBLIC))
            pf = PropertyFlag::IS_PUBLIC;

        if (ENUMBITMASK_ISTRUE(symbol->flags, VariableFlags::CONSTANT)) {
            if (!TIPropertyAdd(clazz.get(), (OObject *) symbol->name, nullptr,
                               PropertyFlag::IS_CONSTANT | pf))
                return {};
        } else if (ENUMBITMASK_ISTRUE(symbol->flags, VariableFlags::VARIABLE)) {
            if (!TIPropertyAdd(clazz.get(), (OObject *) symbol->name, symbol->slot, pf))
                return {};
        }
    }

    // FIXME: traits

    return clazz;
}

HOType orbiter::datatype::TraitTypeInit(Isolate *isolate) {
    auto clazz = MakeType(isolate, InstanceType::CLASS, 0, 0, 0);
    return clazz;
}
