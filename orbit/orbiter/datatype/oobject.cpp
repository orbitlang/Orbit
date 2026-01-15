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

bool orbiter::datatype::EqualStrict(const OObject *left, const OObject *right) {
    return false;
}

bool orbiter::datatype::IsTypeExtends(const TypeInfo *type, const TypeInfo *target) {
    if (type == nullptr || target == nullptr)
        return false;

    if (type == target)
        return true;

    auto *cursor = type;

    if (cursor->mro != nullptr) {
        const auto *mro = (Tuple *) cursor->mro;

        for (auto i = 0; i < mro->length; i++) {
            if (mro->objects[i] == (OObject *) target) {
                cursor = target;

                break;
            }
        }
    }

    while (cursor != nullptr) {
        if (cursor == target)
            break;

        cursor = O_GET_TYPE(cursor);
    }

    return cursor != nullptr;
}

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, const char *name, OObject *value, U16 slot, PropertyFlag flags) {
    auto orname = ORStringIntern(type->isolate, name);
    if (!orname)
        return false;

    return TIPropertyAdd(type, (OObject *) orname.get(), value, slot, flags);
}

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, OObject *name, OObject *value, U16 slot, PropertyFlag flags) {
    PropertyDescriptor tmp{};

    auto *orname = (ORString *) name;

    assert(orname != nullptr && O_GET_TYPE(orname)->i_type == InstanceType::STRING);

    int i = 0;
    for (; i < type->properties.count; i++) {
        auto *property = type->properties.p_array + i;

        if (property->name == nullptr || ORStringCompare(orname, property->name) <= 0) {
            if (property->name != nullptr)
                tmp = *property;

            property->name = O_FAST_INCREF(orname);
            property->slot = slot;

            if (ENUMBITMASK_ISFALSE(flags, PropertyFlag::IN_OBJECT))
                property->value = O_INCREF(value);

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

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, const FunctionDef *bulk, PropertyFlag flags) {
    flags &= ~PropertyFlag::IN_OBJECT; // Clear IN_OBJECT flag since it's not applicable in this context

    for (auto *cursor = bulk; cursor->name != nullptr; cursor++) {
        auto fn = FunctionNew(type->isolate, cursor);
        if (!fn)
            return false;

        if (!TIPropertyAdd(type, cursor->name, (OObject *) fn.get(), 0, flags))
            return false;
    }

    return true;
}

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, const OPropertyEntry *bulk) {
    if (bulk != nullptr) {
        for (auto *cursor = bulk; cursor->name != nullptr; cursor++) {
            const auto flags = bulk->details | PropertyFlag::IN_OBJECT;

            if (!TIPropertyAdd(type, cursor->name, nullptr, bulk->slot, flags))
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

    memory::IsolateAllocator allocator(isolate);
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
    int high = type->properties.count - 1;

    while (low <= high) {
        const auto mid = low + (high - low) / 2;

        const auto cmp = ORStringCompare(name, property[mid].name);
        if (cmp == 0)
            return property + mid;

        if (cmp < 0)
            high = mid - 1;
        else
            low = mid + 1;
    }

    return nullptr;
}

PropertyDescriptor *orbiter::datatype::TIFindProperty(const TypeInfo *type, const TypeInfo **out_type,
                                                      const char *name) {
    PropertyDescriptor *prop = nullptr;

    while (prop == nullptr && type != nullptr) {
        // First search in local properties of current type
        prop = TIFindLocalProperty(type, name);

        // Then search in traits if present, using Method Resolution Order (MRO)
        if (prop == nullptr && type->mro != nullptr) {
            const auto *mro = (Tuple *) type->mro;

            // Iterate through all traits in MRO order until property is found
            for (auto i = 0; i < mro->length && prop == nullptr; i++)
                prop = TIFindLocalProperty((TypeInfo *) mro->objects[i], name);
        }

        if (out_type != nullptr && prop != nullptr)
            *out_type = type;

        // If not found, continue search in parent class
        type = O_GET_TYPE(type);
    }

    return prop;
}

HOType orbiter::datatype::MakeType(Isolate *isolate, TypeInfo *super, InstanceType type,
                                   U8 headroom, U8 props, U8 slots) {
    auto *ti = (TypeInfo *) isolate->gc->AllocObject(sizeof(TypeInfoOps));
    if (ti == nullptr)
        return {};

    O_GET_HEAD(ti).type_ = nullptr;
    O_GET_HEAD(ti).is_instance = false;

    U16 offset = sizeof(OObject);
    if (super != nullptr) {
        O_GET_HEAD(ti).type_ = O_INCREF(super);
        offset = super->i_size;
    }

    ti->i_type = type;

    ti->i_size = offset + headroom + (slots * sizeof(void *));
    ti->offset = offset;

    ti->headroom = headroom;

    ti->mro = nullptr;
    ti->isolate = isolate;
    ti->trace = nullptr;

    ti->aux.data = nullptr;
    ti->aux.dtor = nullptr;

    ti->properties.count = 0;
    ti->properties.p_array = nullptr;

    if (!TIPropertiesInit(isolate, ti, props)) {
        isolate->gc->Free((OObject *) ti);

        return {};
    }

    memory::MemoryZero(((unsigned char *) ti) + sizeof(TypeInfo), sizeof(TypeOps));

    O_GC_TRACK_RETURN(isolate, ti, false);
}

U32 orbiter::datatype::GetTypeName(const OObject *object, char *out_str, const U32 out_size) {
    U32 length = 0;
    InstanceType type;

    if (!O_IS_OBJECT(object)) {
        if (object == nullptr)
            type = InstanceType::NIL;
        else if (O_IS_SMI(object))
            type = InstanceType::NUMBER;
        else
            type = InstanceType::BOOLEAN;
    } else {
        type = O_GET_TYPE(object)->i_type;

        if (type == InstanceType::CLASS) {
            /// FIXME: return name of class here
            /// length = ....
            assert(false);
        }
    }

    if (length == 0)
        length = (U32) strlen(InstanceTypeNames[(int) type]);

    if (out_str != nullptr) {
        const auto min_length = std::min(length, out_size);

        memory::MemoryCopy(out_str, InstanceTypeNames[(int) type], min_length);
        out_str[min_length] = '\0';
    }

    return length;
}
