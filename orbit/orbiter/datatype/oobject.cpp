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

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, const char *name, OObject *value, PropertyFlag flags) {
    auto orname = ORStringIntern(type->isolate, name);
    if (!orname)
        return false;

    return TIPropertyAdd(type, (OObject *) orname.get(), value, flags);
}

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, OObject *name, OObject *value, PropertyFlag flags) {
    PropertyDescriptor tmp{};

    auto *orname = (ORString *) name;

    assert(orname != nullptr && O_GET_TYPE(orname)->i_type == InstanceType::STRING);

    int i = 0;
    for (; i < type->properties.count; i++) {
        auto *property = type->properties.p_array + i;

        if (property->name == nullptr || ORStringCompare(orname, property->name) <= 0) {
            if (property->name != nullptr)
                tmp = *property;

            property->name = O_INCREF(orname);

            property->value = value;

            property->detail = flags;

            break;
        }
    }

    if (tmp.name != nullptr) {
        for (i += 1; i < type->properties.count; i++) {
            auto *property = type->properties.p_array + i;
            auto cmp = ORStringCompare(orname, property->name);

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

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, const FunctionDef *bulk) {
    for (auto *cursor = bulk; cursor->name != nullptr; cursor++) {
        auto *fn = FunctionNew(type->isolate, cursor);
        if (fn == nullptr)
            return false;

        if (!TIPropertyAdd(type, cursor->name, (OObject *) fn, {})) {
            Release(fn);

            return false;
        }
    }

    return true;
}

bool orbiter::datatype::TIPropertiesInit(Isolate *isolate, TypeInfo *type, U8 n) {
    assert(type->properties.p_array == nullptr);

    if (n == 0) {
        type->properties.p_array = nullptr;
        type->properties.count = 0;

        return true;
    }

    IsolateAllocator allocator(isolate);
    auto *tmp = allocator.calloc<PropertyDescriptor>(sizeof(PropertyDescriptor) * n);
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

TypeInfo *orbiter::datatype::MakeType(Isolate *isolate, TypeInfo *super, InstanceType type,
                                      U8 headroom, U8 props, U8 slots) {
    IsolateAllocator allocator(isolate);

    auto *ti = allocator.alloc<TypeInfo>(sizeof(TypeInfo));
    if (ti == nullptr)
        return nullptr;

    O_UNSAFE_GET_RC(ti) = (MSize) memory::RCType::INLINE;
    O_GET_HEAD(ti).type_ = nullptr;

    U16 offset = sizeof(OObject);
    if (super != nullptr) {
        O_GET_HEAD(ti).type_ = O_INCREF(super);
        offset = super->i_size;
    }

    ti->i_type = type;

    ti->i_size = offset + headroom + (slots * sizeof(void *));
    ti->offset = offset;

    ti->headroom = headroom;

    ti->isolate = isolate;

    ti->aux.data = nullptr;
    ti->aux.dtor = nullptr;

    ti->properties.count = 0;
    ti->properties.p_array = nullptr;

    if (!TIPropertiesInit(isolate, ti, props)) {
        allocator.free(ti);

        return nullptr;
    }

    return ti;
}
