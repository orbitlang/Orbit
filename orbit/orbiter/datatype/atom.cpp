// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/orbiter/datatype/hashmap.h>

#include <orbit/orbiter/datatype/atom.h>

using namespace orbiter::datatype;

using GATEntry = HEntry<ORString *, Atom *>;
using GATMap = HashMap<
    ORString *,
    Atom *,
    ORStringEqual,
    ORStringHash
>;

class GAT {
public:
    std::shared_mutex lock;
    GATMap map;

    explicit GAT(orbiter::Isolate *isolate) : map(isolate) {
    }
};

bool AtomDtor(Atom *self) {
    O_FAST_DECREF(self->id);

    self->id = nullptr;

    return true;
}

bool AtomGATDtor(TypeInfo *self) {
    auto *gat = (GAT *) self->aux.data;

    gat->map.Finalize([](const GATEntry *entry) {
        O_FAST_DECREF(entry->key);
        O_FAST_DECREF(entry->value);
    });

    const orbiter::memory::IsolateAllocator allocator(self->isolate);
    allocator.FreeObject(gat);

    self->aux.data = nullptr;

    return true;
}

/// `str(@name)`: returns just the identifier, without the leading `@`.
static OObject *AtomToString(orbiter::Isolate *isolate, const OObject *self) {
    return (OObject *) ((const Atom *) self)->id;
}

/// `repr(@name)`: returns the Orbit source literal, i.e. `@name`.
static OObject *AtomToRepr(orbiter::Isolate *isolate, const OObject *self) {
    const auto s = ORStringFormat(isolate, "@%s", ORSTRING_TO_CSTR(((const Atom *) self)->id));
    return s ? (OObject *) s.get() : nullptr;
}

const OPropertyEntry atom_props[] = {
    OPROPERTY_ENTRY("name", 0, PropertyFlag::IS_CONSTANT|PropertyFlag::IS_PUBLIC),

    OPROPERTY_SENTINEL
};

bool orbiter::datatype::AtomTypeSetup(TypeInfo *self) {
    memory::IsolateAllocator allocator(self->isolate);

    auto *gat = allocator.AllocObject<GAT>(self->isolate);
    if (gat == nullptr)
        return false;

    if (!gat->map.Initialize()) {
        allocator.FreeObject(gat);

        return false;
    }

    self->dtor = (DtorFn) AtomDtor;

    self->aux.data = gat;
    self->aux.dtor = AtomGATDtor;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.to_string = AtomToString;
    ops.to_repr = AtomToRepr;

    return TIPropertyAdd(self, atom_props);
}

HAtom orbiter::datatype::AtomNew(Isolate *isolate, const char *string, const MSize length) {
    const auto id = ORStringNew(isolate, string, length);
    if (!id)
        return {};

    return AtomNew(isolate, id.get());
}

HAtom orbiter::datatype::AtomNew(Isolate *isolate, ORString *id) {
    auto *gat = (GAT *) isolate->primitive[(int) InstanceType::ATOM]->aux.data;
    assert(gat != nullptr);

    GATEntry *entry;

    std::shared_lock shared_lock(gat->lock);

    if (gat->map.Lookup(id, &entry) == LookupResult::OK)
        return HAtom(entry->value);

    shared_lock.unlock();

    std::unique_lock lock(gat->lock);

    if (gat->map.Lookup(id, &entry) == LookupResult::OK)
        return HAtom(entry->value);

    if ((entry = gat->map.AllocHEntry()) == nullptr)
        return {};

    auto *atom = MakeObject<Atom>(isolate, InstanceType::ATOM);
    if (atom == nullptr) {
        gat->map.FreeHEntry(entry);

        return {};
    }

    entry->key = id;
    entry->value = atom;

    if (gat->map.Insert(entry) == LookupResult::OK) {
        atom->id = O_FAST_INCREF(id);

        O_FAST_INCREF(id); // GAT key
        O_FAST_INCREF(atom); // GAT value (atom)

        lock.unlock();

        O_GC_TRACK_RETURN(isolate, atom, false);
    }

    isolate->gc->RawFree((OObject *) atom, false);

    gat->map.FreeHEntry(entry);

    return {};
}

HOType orbiter::datatype::AtomTypeInit(Isolate *isolate) {
    auto atom = MakeType(isolate, "Atom", InstanceType::ATOM, 0, 1, 1);
    return atom;
}
