// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <orbit/orbiter/datatype/ctbuilder.h>

using namespace orbiter::datatype;

bool PushProperties(TypeInfo *type, const ExportedSymbol *exported, const U16 length) {
    for (auto i = 0; i < length; i++) {
        const auto *symbol = exported + i;

        PropertyFlag pf{};
        if (ENUMBITMASK_ISTRUE(symbol->flags, orbiter::VariableFlags::PUBLIC))
            pf = PropertyFlag::IS_PUBLIC;

        if (ENUMBITMASK_ISTRUE(symbol->flags, orbiter::VariableFlags::CONSTANT)) {
            if (!TIPropertyAdd(type, (OObject *) symbol->name, nullptr, PropertyFlag::IS_CONSTANT | pf))
                return false;
        } else if (ENUMBITMASK_ISTRUE(symbol->flags, orbiter::VariableFlags::VARIABLE)) {
            if (!TIPropertyAdd(type, (OObject *) symbol->name, symbol->slot, pf))
                return false;
        }
    }

    return true;
}

bool orbiter::datatype::ClassTypeSetup(TypeInfo *self) {
    return true;
}

bool orbiter::datatype::TraitTypeSetup(TypeInfo *self) {
    return true;
}

HClass orbiter::datatype::ClassNew(TypeInfo *type) {
    const auto *isolate = O_GET_ISOLATE(type);

    if (type->i_type != InstanceType::CLASS) {
        // FIXME: error
        assert(false);
    }

    auto *clazz = MakeObject<Class>(type);

    if (clazz != nullptr) {
        // TODO ...
    }

    O_GC_TRACK_RETURN(isolate, clazz, true);
}

HOType orbiter::datatype::ClassTypeInit(Isolate *isolate) {
    auto clazz = MakeType(isolate, InstanceType::CLASS, 0, 0, 0);
    return clazz;
}

HOType orbiter::datatype::ClassTypeNew(const Code *code, TypeInfo *super, TypeInfo **traits, U16 traits_count) {
    const auto isolate = O_GET_ISOLATE(code);
    HOType clazz{};

    if (super == nullptr)
        clazz = MakeTypeExtended(isolate, InstanceType::CLASS, 0, code->exported.length, code->slots_count);
    else
        clazz = MakeType(isolate, super, InstanceType::CLASS, 0, code->exported.length, code->slots_count);

    if (!clazz) {
        if (!PushProperties(clazz.get(), code->exported.symbols, code->exported.length))
            return {};

        // FIXME: traits
    }

    return clazz;
}

HOType orbiter::datatype::TraitTypeInit(Isolate *isolate) {
    auto clazz = MakeType(isolate, InstanceType::TRAIT, 0, 0, 0);
    return clazz;
}

HOType orbiter::datatype::TraitTypeNew(const Code *code, TypeInfo **traits, U16 traits_count) {
    const auto isolate = O_GET_ISOLATE(code);

    auto trait = MakeTypeExtended(isolate, InstanceType::TRAIT, 0, code->exported.length, 0);
    if (trait) {
        if (!PushProperties(trait.get(), code->exported.symbols, code->exported.length))
            return {};
    }

    return trait;
}
