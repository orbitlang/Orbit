// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

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

bool AtomGATDtor(TypeInfo *self) {
    auto *gat = (GATMap *) self->aux.data;

    gat->Finalize([](const GATEntry *entry) {
        Release(entry->key);
        Release(entry->value);
    });

    orbiter::IsolateAllocator allocator(self->isolate);
    allocator.FreeObject(self);

    self->aux.data = nullptr;

    return true;
}

bool orbiter::datatype::AtomTypeSetup(TypeInfo *self) {
    IsolateAllocator allocator(self->isolate);

    auto *map = allocator.AllocObject<GATMap>(self->isolate);
    if (map == nullptr)
        return false;

    if (!map->Initialize()) {
        allocator.FreeObject(map);

        return false;
    }

    self->aux.data = map;
    self->aux.dtor = AtomGATDtor;

    return true;
}

HAtom orbiter::datatype::AtomNew(Isolate *isolate, const char *string, MSize length) {
    const auto id = ORStringNew(isolate, string, length);
    if (!id)
        return {};

    return AtomNew(isolate, id.get());
}

HAtom orbiter::datatype::AtomNew(Isolate *isolate, ORString *id) {
    auto *gat = (GATMap *) isolate->primitive[(int) InstanceType::ATOM]->aux.data;
    GATEntry *entry;

    assert(gat != nullptr);

    if (gat->Lookup(id, &entry))
        return HAtom(O_INCREF(entry->value));

    if ((entry = gat->AllocHEntry()) == nullptr)
        return {};

    auto *atom = MakeObject<Atom>(isolate, InstanceType::ATOM);
    if (atom == nullptr) {
        gat->FreeHEntry(entry);

        return {};
    }

    entry->key = id;
    entry->value = atom;

    if (gat->Insert(entry)) {
        atom->id = O_INCREF(id);

        O_INCREF(id);
        O_INCREF(atom);

        return HAtom(atom);
    }

    Release(atom);

    gat->FreeHEntry(entry);

    return {};
}

TypeInfo *orbiter::datatype::AtomTypeInit(Isolate *isolate) {
    auto *atom = MakeType(isolate, InstanceType::ATOM, 0, 0, 1);
    return atom;
}
