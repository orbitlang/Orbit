// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/util/hashmap.h>

#include <orbit/orbiter/datatype/atom.h>

using namespace orbiter::datatype;

using GATEntry = HEntry<ORString *, Atom *>;
using GATMap = HashMap<
    ORString *,
    Atom *,
    orbiter::memory::Alloc,
    orbiter::memory::Realloc,
    orbiter::memory::Free,
    ORStringEqual,
    ORStringHash
>;

bool AtomGATDtor(TypeInfo *self) {
    auto *gat = (GATMap *) self->aux.data;

    gat->Finalize([](const GATEntry *entry) {
        Release(entry->key);
        Release(entry->value);
    });

    self->aux.data = nullptr;

    return true;
}

bool orbiter::datatype::AtomTypeSetup(Context *ctx, TypeInfo *self) {
    auto *map = (GATMap *) memory::Alloc((sizeof(GATMap)));
    if (map == nullptr)
        return false;

    if (!map->Initialize()) {
        memory::Free(map);

        return false;
    }

    self->aux.data = map;
    self->aux.dtor = AtomGATDtor;

    return true;
}

HAtom orbiter::datatype::AtomNew(const Context *ctx, const char *string, MSize length) {
    const auto id = ORStringNew(ctx, string, length);
    if (!id)
        return {};

    return AtomNew(ctx, id.get());
}

HAtom orbiter::datatype::AtomNew(const Context *ctx, ORString *id) {
    auto *gat = (GATMap *) ctx->primitive[(int) InstanceType::ATOM]->aux.data;
    GATEntry *entry;

    assert(gat != nullptr);

    if (gat->Lookup(id, &entry))
        return HAtom(O_INCREF(entry->value));

    if ((entry = gat->AllocHEntry()) == nullptr)
        return {};

    auto *atom = MakeObject<Atom>(ctx, InstanceType::ATOM);
    if (atom == nullptr) {
        gat->FreeHEntry(entry);

        return {};
    }

    entry->key = id;
    entry->value = atom;

    if (gat->Insert(entry)) {
        O_INCREF(id);
        O_INCREF(atom);

        return HAtom(atom);
    }

    Release(atom);

    gat->FreeHEntry(entry);

    return {};
}

TypeInfo *orbiter::datatype::AtomTypeInit(Context *ctx) {
    auto *atom = MakeType(ctx, InstanceType::ATOM, 0, 0, 1);
    return atom;
}
