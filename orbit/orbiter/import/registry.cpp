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

orbiter::import::ModuleEntry *orbiter::import::ModuleEntryNew(Isolate *isolate, ORString *name) {
    memory::IsolateAllocator allocator(isolate);

    auto *entry = allocator.AllocObject<ModuleEntry>();
    if (entry == nullptr)
        return nullptr;

    // `owner` stays nullptr: for a SOURCE module the executor fiber does
    // not exist yet — it is set later by `Importer::BlockOnExecutor`.
    // BUILTIN/VIRTUAL entries never get an owner (no top-level).
    entry->name = O_FAST_INCREF(name);

    return entry;
}

void orbiter::import::ModuleEntryDel(Isolate *isolate, ModuleEntry *entry) {
    if (entry == nullptr)
        return;

    // O_FAST_DECREF is null-safe, so unset `module`/`spec` cost nothing.
    // `waiters` must already be drained — the FiberQueue destructor (run
    // by FreeObject) asserts count_==0. `Commit`/`Fail` are the only paths
    // that drop entries, and both drain before reaching us.
    O_FAST_DECREF(entry->name);
    O_FAST_DECREF(entry->module);
    O_FAST_DECREF(entry->spec);

    memory::IsolateAllocator(isolate).FreeObject(entry);
}
