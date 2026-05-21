// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/memory/iallocator.h>

#include <orbit/orbiter/import/registry.h>

using namespace orbiter::datatype;
using namespace orbiter::import;

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

ModuleEntry *orbiter::import::ModuleEntryNew(Isolate *isolate, ORString *name) {
    memory::IsolateAllocator allocator(isolate);

    auto *entry = allocator.calloc<ModuleEntry>(sizeof(ModuleEntry));
    if (entry == nullptr)
        return nullptr;

    entry->name = O_FAST_INCREF(name);
    entry->state = ModuleState::LOADING;

    return entry;
}

void orbiter::import::ModuleEntryDel(Isolate *isolate, ModuleEntry *entry) {
    if (entry == nullptr)
        return;

    // O_FAST_DECREF is null-safe, so unset `module`/`spec` cost nothing.
    O_FAST_DECREF(entry->name);
    O_FAST_DECREF(entry->module);
    O_FAST_DECREF(entry->spec);

    memory::IsolateAllocator(isolate).free(entry);
}
