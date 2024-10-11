// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/oobject.h>

using namespace orbiter::datatype;

bool orbiter::datatype::Equal(const OObject *left, const OObject *right) {
    return false;
}

bool orbiter::datatype::TIPropertyAdd(const Context *ctx, TypeInfo *type, const char *name,
                                      OObject *value, PropertyDetail detail) {
    PropertyDescriptor tmp{};
    int i = 0;

    auto orname = ORStringIntern(ctx, name);
    if (!orname)
        return false;

    auto r_orname = orname.get();

    for (; i < type->properties.count; i++) {
        auto *property = type->properties.p_array + i;

        if (property->name == nullptr || ORStringCompare(r_orname, property->name) <= 0) {
            if (property->name != nullptr)
                tmp = *property;

            property->name = orname.release();

            // TODO: offset + slot of super type

            property->value = value;

            property->detail = detail;

            break;
        }
    }

    if (tmp.name != nullptr) {
        for (i += 1; i < type->properties.count; i++) {
            auto *property = type->properties.p_array + i;
            auto cmp = ORStringCompare(r_orname, property->name);

            if (cmp < 0) {
                auto swap = *property;

                *property = tmp;

                tmp = swap;
            } else if (cmp == 0) {
                *property = tmp;
                break;
            }
        }
    }

    return true;
}

bool orbiter::datatype::TIPropertyAdd(const Context *ctx, TypeInfo *type, const FunctionDef *bulk) {
    for (auto *cursor = bulk; cursor->name != nullptr; cursor++) {
        auto *fn = FunctionNew(ctx, cursor);
        if (fn == nullptr)
            return false;

        if (!TIPropertyAdd(ctx, type, cursor->name, (OObject *) fn, {})) {
            Release(fn);

            return false;
        }
    }

    return true;
}

bool orbiter::datatype::TIPropertiesInit(TypeInfo *type, U8 n) {
    assert(type->properties.p_array == nullptr);

    if (n == 0) {
        type->properties.p_array = nullptr;
        type->properties.count = 0;

        return true;
    }

    auto *tmp = (PropertyDescriptor *) memory::Calloc(sizeof(PropertyDescriptor) * n);
    if (tmp == nullptr)
        return false;

    type->properties.p_array = tmp;
    type->properties.count = n;

    return true;
}

MSize orbiter::datatype::Hash(const OObject *obj) {
    // TODO: IMPL
    return 0;
}

PropertyDescriptor *orbiter::datatype::TIFindLocalProperty(const TypeInfo *type, const char *name) {
    auto *property = type->properties.p_array;

    int low = 0;
    int high = type->properties.count;
    while (low < high) {
        const auto mid = low + (high - low) / 2;

        const auto cmp = ORStringCompare(property[mid].name, name);
        if (cmp == 0)
            return property + mid;

        if (cmp < 0)
            low = mid + 1;
        else
            high = mid - 1;
    }

    return nullptr;
}

PropertyDescriptor *orbiter::datatype::TIFindProperty(const TypeInfo *type, const char *name) {
    auto *prop = TIFindLocalProperty(type, name);

    type = O_GET_TYPE(type);

    while (prop == nullptr && type != nullptr) {
        prop = TIFindLocalProperty(type, name);
        type = O_GET_TYPE(type);
    }

    return prop;
}

TypeInfo *orbiter::datatype::MakeType(TypeInfo *super, InstanceType type, U8 headroom, U8 props, U8 slots) {
    auto *ti = (TypeInfo *) memory::Alloc(sizeof(TypeInfo));
    if (ti == nullptr)
        return nullptr;

    O_UNSAFE_GET_RC(ti) = (MSize) memory::RCType::INLINE;
    O_GET_HEAD(ti).type_ = O_VFY_INCREF(super);

    ti->i_type = type;

    ti->i_size = sizeof(OObject) + headroom + (slots * sizeof(void *));

    ti->headroom = headroom;

    if (!TIPropertiesInit(ti, props)) {
        memory::Free(ti);

        return nullptr;
    }

    return ti;
}
