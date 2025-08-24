// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/c3/c3.h>

#include <orbit/orbiter/datatype/ctbuilder.h>

using namespace orbiter::datatype;

bool ClassBlueprintDtor(TypeInfo *self) {
    if (self->aux.data != nullptr) {
        const orbiter::memory::IsolateAllocator allocator(O_GET_ISOLATE(self));

        allocator.free(self->aux.data);

        self->aux.data = nullptr;
    }

    return true;
}

bool InitObjectBlueprint(orbiter::Isolate *isolate, TypeInfo *type) {
    if (type->i_type != InstanceType::CLASS || type->aux.data != nullptr)
        return true;

    orbiter::memory::IsolateAllocator allocator(isolate);

    auto *blueprint = allocator.calloc<unsigned char>(type->i_size - sizeof(OObject));
    if (blueprint == nullptr)
        return {};

    auto **slot = (OObject **) (blueprint + (type->offset - sizeof(OObject)) + type->headroom);
    for (auto i = 0; i < type->properties.count; i++) {
        const auto *property = type->properties.p_array + i;

        if (ENUMBITMASK_ISTRUE(property->detail, PropertyFlag::DUP_INLINE))
            slot[property->slot] = property->value;
    }

    auto *super = O_GET_TYPE(type);
    if (super->i_type == InstanceType::CLASS && super->i_size > super->offset) {
        if (super->aux.data == nullptr) {
            if (!InitObjectBlueprint(isolate, super))
                return false;
        }

        orbiter::memory::MemoryCopy(blueprint, super->aux.data, type->offset - sizeof(OObject));
    }

    type->aux.data = blueprint;

    return true;
}

bool PushProperties(TypeInfo *type, const ExportedSymbol *exported, const U16 length) {
    for (auto i = 0; i < length; i++) {
        PropertyFlag pf{};

        const auto *symbol = exported + i;

        if (ENUMBITMASK_ISTRUE(symbol->flags, orbiter::VariableFlags::PUBLIC))
            pf = PropertyFlag::IS_PUBLIC;

        if (ENUMBITMASK_ISTRUE(symbol->flags, orbiter::VariableFlags::CP_INLINE))
            pf |= PropertyFlag::DUP_INLINE;

        pf |= ENUMBITMASK_ISTRUE(symbol->flags, orbiter::VariableFlags::CONSTANT)
                  ? PropertyFlag::IS_CONSTANT
                  : PropertyFlag::IN_OBJECT;

        if (!TIPropertyAdd(type, (OObject *) symbol->name, symbol->slot, pf))
            return false;
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
    auto *isolate = O_GET_ISOLATE(type);

    if (type->i_type != InstanceType::CLASS) {
        // FIXME: error
        assert(false);
    }

    if (type->aux.data == nullptr) {
        if (!InitObjectBlueprint(isolate, type))
            return {};
    }

    auto *clazz = MakeObject<Class>(type);
    if (clazz == nullptr)
        return {};

    memory::MemoryCopy(((unsigned char *) clazz) + sizeof(OObject),
                       type->aux.data,
                       type->i_size - sizeof(OObject));

    O_GC_TRACK_RETURN(isolate, clazz, true);
}

HOType orbiter::datatype::ClassTypeInit(Isolate *isolate) {
    auto clazz = MakeType(isolate, InstanceType::CLASS, 0, 0, 0);
    return clazz;
}

HOType orbiter::datatype::ClassTypeNew(const Code *code, TypeInfo *super, TypeInfo **traits, const U16 traits_count) {
    const auto isolate = O_GET_ISOLATE(code);
    HOType clazz{};

    if (super == nullptr)
        clazz = MakeTypeExtended(isolate, InstanceType::CLASS, 0, code->exported.length, code->slots_count);
    else
        clazz = MakeType(isolate, super, InstanceType::CLASS, 0, code->exported.length, code->slots_count);

    if (!clazz)
        return {};

    if (!PushProperties(clazz.get(), code->exported.symbols, code->exported.length))
        return {};

    linearization::C3 c3(clazz.get());
    if (!c3.BuildMRO(traits, traits_count))
        return {};

    clazz->aux.dtor = ClassBlueprintDtor;

    return clazz;
}

HOType orbiter::datatype::TraitTypeInit(Isolate *isolate) {
    auto clazz = MakeType(isolate, InstanceType::TRAIT, 0, 0, 0);
    return clazz;
}

HOType orbiter::datatype::TraitTypeNew(const Code *code, TypeInfo **traits, const U16 traits_count) {
    const auto isolate = O_GET_ISOLATE(code);

    auto trait = MakeTypeExtended(isolate, InstanceType::TRAIT, 0, code->exported.length, 0);
    if (trait) {
        if (!PushProperties(trait.get(), code->exported.symbols, code->exported.length))
            return {};

        linearization::C3 c3(trait.get());
        if (!c3.BuildMRO(traits, traits_count))
            return {};
    }

    return trait;
}
