// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/function.h>

#include <orbit/orbiter/datatype/oobject.h>

using namespace orbiter::datatype;

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

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, const char *name, OObject *value, const U16 slot,
                                      const PropertyFlag flags) {
    const auto orname = ORStringIntern(type->isolate, name);
    if (!orname)
        return false;

    return TIPropertyAdd(type, (OObject *) orname.get(), value, slot, flags);
}

bool orbiter::datatype::TIPropertyAdd(TypeInfo *type, OObject *name, OObject *value, const U16 slot,
                                      const PropertyFlag flags) {
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
        auto fn = FunctionNew(type->isolate, type, cursor);
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

            if (!TIPropertyAdd(type, cursor->name, nullptr, cursor->slot, flags))
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

HOType orbiter::datatype::MakeType(Isolate *isolate, TypeInfo *super, const char *name, const InstanceType type,
                                   const U8 headroom, const U8 props, const U8 slots) {
    auto *ti = (TypeInfo *) isolate->gc->AllocObject(sizeof(TypeInfoOps));
    if (ti == nullptr)
        return {};

    const memory::IsolateAllocator allocator(isolate);

    if (name != nullptr) {
        const auto slen = strlen(name);

        ti->name = (char *) allocator.Alloc(slen + 1);
        if (ti->name == nullptr) {
            isolate->gc->RawFree((OObject *) ti, false);

            return {};
        }

        memory::MemoryCopy((char *) ti->name, name, slen);
        ((char *) ti->name)[slen] = '\0';
    }

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
    ti->trace = nullptr;
    ti->ctor = nullptr;
    ti->dtor = nullptr;
    ti->mro = nullptr;

    ti->aux.data = nullptr;
    ti->aux.dtor = nullptr;

    ti->properties.p_array = nullptr;
    ti->properties.count = 0;
    ti->properties.origin = 0;

    if (!TIPropertiesInit(isolate, ti, props)) {
        allocator.free((char *) ti->name);

        isolate->gc->RawFree((OObject *) ti, false);

        return {};
    }

    memory::MemoryZero(((unsigned char *) ti) + sizeof(TypeInfo), sizeof(TypeOps));

    O_GC_TRACK_RETURN(isolate, ti, false);
}

int orbiter::datatype::MonitorAcquire(Fiber *fiber, OObject *object, const bool can_block) noexcept {
    auto *monitor = O_GET_MON(object).load(std::memory_order_relaxed);
    if (monitor == nullptr) {
        memory::IsolateAllocator allocator(O_GET_ISOLATE(object));

        monitor = allocator.AllocObject<sync::Monitor>();
        if (monitor == nullptr)
            return -1;

        // A freshly allocated monitor cannot be contended: this acquire always
        // succeeds regardless of can_block.
        monitor->Acquire(fiber, can_block);

        sync::Monitor *expected = nullptr;
        if (O_GET_MON(object).compare_exchange_strong(expected, monitor, std::memory_order_acq_rel))
            return 1;

        allocator.FreeObject(monitor);

        monitor = expected;
    }

    if (monitor->Acquire(fiber, can_block))
        return 1;

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

TypeInfo *orbiter::datatype::GetSuper(const TypeInfo *base, const TypeInfo *owner) {
    if (base == nullptr || owner == nullptr)
        return nullptr;

    for (const auto *cls = base; cls != nullptr; cls = O_GET_TYPE(cls)) {
        const auto *mro = (Tuple *) cls->mro;

        // `owner` is this class itself: go to its first trait, else its superclass.
        if (cls == owner)
            return (mro != nullptr && mro->length > 0) ? (TypeInfo *) mro->objects[0] : O_GET_TYPE(cls);

        // `owner` is one of this class's traits: the next trait, or (when it is
        // the last one) this class's superclass.
        if (mro != nullptr) {
            for (auto i = 0; i < mro->length; i++) {
                if (mro->objects[i] == (OObject *) owner)
                    return (i + 1 < mro->length) ? (TypeInfo *) mro->objects[i + 1] : O_GET_TYPE(cls);
            }
        }
    }

    return nullptr;
}

U32 orbiter::datatype::GetTypeName(const Isolate *isolate, const OObject *object, char *out_str, const U32 out_size) {
    const TypeInfo *type = nullptr;

    if (!O_IS_OBJECT(object)) {
        if (O_IS_SMI(object))
            type = isolate->primitive[(int) InstanceType::NUMBER];
        else
            type = isolate->primitive[(int) InstanceType::BOOLEAN];
    } else {
        if (O_GET_RC(object).IsInstance())
            type = O_GET_TYPE(object);
        else
            type = (TypeInfo *) object;
    }

    auto length = (U32) 3; // 'nil'

    if (type != nullptr)
        length = strlen(type->name);

    if (out_str != nullptr) {
        const auto min_length = std::min(length, out_size);

        memory::MemoryCopy(out_str, type != nullptr ? type->name : "nil", min_length);

        out_str[min_length] = '\0';
    }

    return length;
}

void orbiter::datatype::MonitorDestroy(OObject *object) noexcept {
    sync::Monitor *expected = O_GET_MON(object).load(std::memory_order_relaxed);

    if (expected == nullptr)
        return;

    if (O_GET_MON(object).compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
        assert(expected->IsDestroyable());

        const memory::IsolateAllocator allocator(O_GET_ISOLATE(object));
        allocator.FreeObject(expected);
    }
}

void orbiter::datatype::MonitorRelease(const OObject *object) noexcept {
    auto *monitor = O_GET_MON(object).load(std::memory_order_acquire);
    if (monitor != nullptr)
        monitor->Release();
}
