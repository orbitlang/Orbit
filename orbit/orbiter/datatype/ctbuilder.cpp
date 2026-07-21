// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/c3/c3.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/number.h>

#include <orbit/orbiter/datatype/ctbuilder.h>

using namespace orbiter::datatype;

static bool ClassCompare(const OObject *left, const OObject *right, int &result);

static bool ClassEqual(const OObject *left, const OObject *right, bool &out);

static OObject *ClassToString(orbiter::Isolate *isolate, const Class *self);

static OObject *ClassToRepr(orbiter::Isolate *isolate, const Class *self);

static MSize ClassHash(const OObject *self);

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

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

    const auto slots_size = type->i_size - (type->offset + type->headroom);
    const auto blueprint_size = sizeof(ClassBlueprint) + slots_size;
    auto *blueprint = allocator.calloc<ClassBlueprint>(blueprint_size);
    if (blueprint == nullptr)
        return {};

    auto *super = O_GET_TYPE(type);
    if (super->i_type == InstanceType::CLASS && super->i_size > super->offset) {
        if (super->aux.data == nullptr && !InitObjectBlueprint(isolate, super))
            return false;
    }

    auto **slot = (OObject **) (((unsigned char *) blueprint) + sizeof(ClassBlueprint));
    for (auto i = 0; i < type->properties.count; i++) {
        const auto *property = type->properties.p_array + i;

        if (ENUMBITMASK_ISTRUE(property->detail, PropertyFlag::DUP_INLINE))
            slot[property->slot] = property->value;
    }

    type->aux.data = blueprint;

    // Load magic methods

    // The link to the Equal and Hash method dispatchers is set here instead of at type creation.
    // The reason behind this choice is to avoid breaking invariants and to allow custom objects
    // to behave standardly.

    auto *cache = (ClassBlueprint *) type->aux.data;
    auto &ops = ((TypeInfoOps *) type)->ops;
    const TypeInfo *out_type;

    auto *prop = TIFindProperty(type, &out_type, "equal");
    if (prop != nullptr) {
        cache->equal = (Function *) prop->value;

        ops.equal = ClassEqual;
    }

    prop = TIFindProperty(type, &out_type, "compare");
    if (prop != nullptr) {
        cache->compare = (Function *) prop->value;

        ops.compare = ClassCompare;
    }

    prop = TIFindProperty(type, &out_type, "hash");
    if (prop != nullptr) {
        cache->hash = (Function *) prop->value;

        ops.hash = ClassHash;
    }

    prop = TIFindProperty(type, &out_type, "repr");
    if (prop != nullptr) {
        cache->repr = (Function *) prop->value;

        ops.to_repr = (ToStrFn)ClassToRepr;
    }

    prop = TIFindProperty(type, &out_type, "str");
    if (prop != nullptr) {
        cache->str = (Function *) prop->value;

        ops.to_string = (ToStrFn)ClassToString;
    }

    return true;
}

bool PushProperties(TypeInfo *type, const ExportedSymbol *exported, const U16 length) {
    for (auto i = 0; i < length; i++) {
        PropertyFlag pf{};

        const auto *symbol = exported + i;

        if (ENUMBITMASK_ISTRUE(symbol->flags, orbiter::VariableFlags::PUBLIC))
            pf = PropertyFlag::IS_PUBLIC;
        else if (ENUMBITMASK_ISTRUE(symbol->flags, orbiter::VariableFlags::PROTECTED))
            pf = PropertyFlag::IS_PROTECTED;

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

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

static bool ClassCompare(const OObject *left, const OObject *right, int &result) {
    const auto *cache = (const ClassBlueprint *) O_GET_TYPE(left)->aux.data;

    IntegerUnderlying cmp;

    OObject *argv[] = {
        (OObject *) left,
        (OObject *) right
    };

    OObject *value = nullptr;
    if (!orbiter::Orbiter::EvalSync(cache->compare, argv, 2, &value))
        return false;

    if (!NumberExtract(value, cmp)) {
        char name[24];
        GetTypeName(O_GET_ISOLATE(left), left, name, sizeof(name));

        ErrorSet(O_GET_ISOLATE(left),
                 TypeError::Details[TypeError::Reason::ID],
                 nullptr,
                 "compare of '%s' returned non-integer value",
                 name);

        return false;
    }

    result = (int) cmp;

    return true;
}

static bool ClassEqual(const OObject *left, const OObject *right, bool &out) {
    const auto *cache = (const ClassBlueprint *) O_GET_TYPE(left)->aux.data;

    OObject *argv[] = {
        (OObject *) left,
        (OObject *) right
    };

    OObject *value = nullptr;
    if (!orbiter::Orbiter::EvalSync(cache->equal, argv, 2, &value))
        return false;

    if (!O_IS_TRUE(value) && !O_IS_FALSE(value)) {
        char name[24];
        GetTypeName(O_GET_ISOLATE(left), left, name, sizeof(name));

        ErrorSet(O_GET_ISOLATE(left),
                 TypeError::Details[TypeError::Reason::ID],
                 nullptr,
                 "equal of '%s' returned non-boolean value",
                 name);

        return false;
    }

    out = O_IS_TRUE(value);

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

static OObject *ClassToString(orbiter::Isolate *isolate, const Class *self) {
    const auto *type = O_GET_TYPE(self);
    const auto *cache = (const ClassBlueprint *) type->aux.data;

    OObject *value = nullptr;

    // Do NOT rely on caller's stack — direct calls leave `self` below
    // the sync frame, but indirect paths (e.g. container stringify) don't.
    if (orbiter::Orbiter::EvalSync(cache->str, (OObject **) &self, 1, &value))
        return value;

    return (OObject *) ORStringFormat(isolate, "<%s at %p>", type->name, self).get();
}

static OObject *ClassToRepr(orbiter::Isolate *isolate, const Class *self) {
    const auto *cache = (const ClassBlueprint *) O_GET_TYPE(self)->aux.data;

    OObject *value = nullptr;

    if (orbiter::Orbiter::EvalSync(cache->repr, (OObject **) &self, 1, &value))
        return value;

    return ClassToString(isolate, self);
}

// *********************************************************************************************************************
// TYPE OPS — RUNTIME
// *********************************************************************************************************************

static MSize ClassHash(const OObject *self) {
    const auto *cache = (const ClassBlueprint *) O_GET_TYPE(self)->aux.data;

    OObject *value = nullptr;
    IntegerUnderlying hash = 0;

    if (!orbiter::Orbiter::EvalSync(cache->hash, (OObject **) &self, 1, &value))
        return HASH_ERROR;

    // HASH_ERROR is only meaningful to the caller if an error is pending: EvalSync
    // leaves one on a panicking hook, but a hook returning a non-integer does not.
    if (!NumberExtract(value, hash)) {
        char name[24];
        GetTypeName(O_GET_ISOLATE(self), self, name, sizeof(name));

        ErrorSet(O_GET_ISOLATE(self),
                 TypeError::Details[TypeError::Reason::ID],
                 nullptr,
                 "hash of '%s' returned non-integer value",
                 name);

        return HASH_ERROR;
    }

    return hash;
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

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

    memory::MemoryCopy(((unsigned char *) clazz) + type->offset + type->headroom,
                       ((ClassBlueprint *) type->aux.data)->blueprint,
                       type->i_size - (type->offset + type->headroom));

    O_GC_TRACK_RETURN(isolate, clazz, true);
}

HOType orbiter::datatype::ClassTypeInit(Isolate *isolate) {
    auto clazz = MakeType(isolate, "Class", InstanceType::CLASS, 0, 0, 0);
    return clazz;
}

HOType orbiter::datatype::ClassTypeNew(const Code *code, TypeInfo *super, TypeInfo **traits, const U16 traits_count) {
    const auto isolate = O_GET_ISOLATE(code);
    HOType clazz{};

    if (super == nullptr)
        clazz = MakeTypeExtended(isolate, ORSTRING_TO_CSTR(code->name), InstanceType::CLASS,
                                 0, code->exported.length, code->slots_count);
    else
        clazz = MakeType(isolate, super, ORSTRING_TO_CSTR(code->name), InstanceType::CLASS,
                         0, code->exported.length, code->slots_count);

    if (!clazz)
        return {};

    clazz->properties.origin = (PtrSize) code;

    if (!PushProperties(clazz.get(), code->exported.symbols, code->exported.length))
        return {};

    const linearization::C3 c3(clazz.get());
    if (!c3.BuildMRO(traits, traits_count))
        return {};

    clazz->aux.dtor = ClassBlueprintDtor;

    return clazz;
}

HOType orbiter::datatype::TraitTypeInit(Isolate *isolate) {
    auto clazz = MakeType(isolate, "Trait", InstanceType::TRAIT, 0, 0, 0);
    return clazz;
}

HOType orbiter::datatype::TraitTypeNew(const Code *code, TypeInfo **traits, const U16 traits_count) {
    const auto isolate = O_GET_ISOLATE(code);

    auto trait = MakeTypeExtended(isolate, ORSTRING_TO_CSTR(code->name), InstanceType::TRAIT,
                                  0, code->exported.length, 0);
    if (trait) {
        trait->properties.origin = (PtrSize) code;

        if (!PushProperties(trait.get(), code->exported.symbols, code->exported.length))
            return {};

        const linearization::C3 c3(trait.get());
        if (!c3.BuildMRO(traits, traits_count))
            return {};
    }

    return trait;
}
