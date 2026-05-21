// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/orbiter/import/importer.h>
#include <orbit/orbiter/import/registry.h>

#include <orbit/orbiter/isolate.h>

using namespace orbiter::datatype;
using namespace orbiter::import;

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

Importer::~Importer() {
    this->modules_.Finalize([this](const ModuleMap::HEntry *e) {
        O_FAST_DECREF(e->key);

        ModuleEntryDel(this->isolate_, e->value);
    });
}

bool Importer::Initialize() {
    this->roots_ = ListNew(this->isolate_);
    if (!this->roots_)
        return false;

    return this->modules_.Initialize();
}

bool Importer::AddRoot(const char *path) {
    const auto root = ORStringNew(this->isolate_, path);
    if (!root)
        return false;

    return this->AddRoot(root.get());
}

bool Importer::AddRoot(ORString *path) const {
    return ListAppend(this->roots_.get(), (OObject *) path);
}

LocateResult Importer::Resolve(const ORString *key, Descriptor *out) const {
    static constexpr Locator builtinLocate = {"Builtin", BuiltinLocate};
    static constexpr Locator fsLocate = {"FSSource", FsSourceLocate};

    StringBuilder builder(this->isolate_);

    auto result = builtinLocate.locate(this, key, out);
    if (result != LocateResult::NOT_MINE)
        return result;

    if (!builder.Write((const unsigned char *) builtinLocate.name, strlen(builtinLocate.name), 10))
        return LocateResult::ERROR;

    // TODO: user defined locators here
    // EOL

    result = fsLocate.locate(this, key, out);
    if (result != LocateResult::NOT_MINE)
        return result;

    if (!builder.Write((const unsigned char *) ", ", 2, 0))
        return LocateResult::ERROR;

    if (!builder.Write((const unsigned char *) fsLocate.name, strlen(fsLocate.name), 0))
        return LocateResult::ERROR;

    ErrorSet(this->isolate_,
             ImportError::Details[ImportError::ID], nullptr,
             ImportError::Details[ImportError::MODULE_NOT_FOUND],
             ORSTRING_TO_CSTR(key),
             builder.BuildString());

    return LocateResult::NOT_MINE;
}

HORString orbiter::import::Canonicalize(Isolate *isolate, ORString *raw, const ImportSpec *origin) {
    const auto *rbuf = ORSTRING_TO_CSTR(raw);
    auto rlen = ORSTRING_LENGTH(raw);

    // Empty key.
    if (rlen == 0) {
        ErrorSet(isolate,
                 ImportError::Details[ImportError::ID],
                 nullptr,
                 ImportError::Details[ImportError::INVALID_KEY],
                 "",
                 "empty");

        return {};
    }

    // Builtin opaque scheme: only [A-Za-z0-9_:] allowed after the leading "::";
    // returned verbatim — no filesystem-style rewriting.
    if (rlen >= 2 && rbuf[0] == ':' && rbuf[1] == ':') {
        for (MSize i = 2; i < rlen; i++) {
            const auto c = (unsigned char) rbuf[i];

            if (!isalnum(c) && c != '_' && c != ':') {
                ErrorSet(isolate,
                         ImportError::Details[ImportError::ID],
                         nullptr,
                         ImportError::Details[ImportError::INVALID_KEY],
                         rbuf,
                         "invalid character in builtin namespace");

                return {};
            }
        }

        return HORString(raw);
    }

    // Filesystem-style pipeline. Build the canonical key segment by segment so
    // separator normalization, "//" collapse and "." removal all happen in one
    // pass; a stray ".." short-circuits to ImportError.
    StringBuilder builder(isolate);

    bool empty = true;

    // Leading "./" → relative to dirname(origin->name).
    const bool is_relative = rlen >= 2 && rbuf[0] == '.' && (rbuf[1] == '/' || rbuf[1] == '\\');
    if (is_relative) {
        if (origin == nullptr) {
            ErrorSet(isolate,
                     ImportError::Details[ImportError::ID],
                     nullptr,
                     ImportError::Details[ImportError::NO_ORIGIN],
                     rbuf);

            return {};
        }

        if (origin->Loader() != LoaderKind::SOURCE) {
            ErrorSet(isolate,
                     ImportError::Details[ImportError::ID],
                     nullptr,
                     ImportError::Details[ImportError::INVALID_ORIGIN],
                     ORSTRING_TO_CSTR(raw));

            return {};
        }

        const auto *obuf = ORSTRING_TO_CSTR(origin->name);

        const auto last_slash = ORStringRFind(origin->name, kPathSep);
        if (last_slash >= 0) {
            if (!builder.Write((const unsigned char *) obuf, last_slash, rlen))
                return {};

            empty = false;
        }

        rbuf += 2;
        rlen -= 2;
    }

    // Walk segments split on '/' or '\'. Skip empty and ".", reject "..".
    MSize seg_start = 0;
    for (MSize i = 0; i <= rlen; i++) {
        const bool at_sep = i == rlen || rbuf[i] == '/' || rbuf[i] == '\\';
        if (!at_sep)
            continue;

        const auto seg_len = i - seg_start;

        if (seg_len == 0) {
            // empty segment — collapses "//" and trailing "/"
        } else if (seg_len == 1 && rbuf[seg_start] == '.') {
            // "." — skip
        } else if (seg_len == 2 && rbuf[seg_start] == '.' && rbuf[seg_start + 1] == '.') {
            ErrorSet(isolate,
                     ImportError::Details[ImportError::ID],
                     nullptr,
                     ImportError::Details[ImportError::INVALID_KEY],
                     ORSTRING_TO_CSTR(raw),
                     "contains '..'");

            return {};
        } else {
            if (!empty) {
                if (!builder.Write((const unsigned char *) kPathSep, 1, seg_len))
                    return {};
            }

            if (!builder.Write((const unsigned char *) (rbuf + seg_start), seg_len, 0))
                return {};

            empty = false;
        }

        seg_start = i + 1;
    }

    if (empty) {
        ErrorSet(isolate,
                 ImportError::Details[ImportError::ID],
                 nullptr,
                 ImportError::Details[ImportError::INVALID_KEY],
                 ORSTRING_TO_CSTR(raw),
                 "empty after normalization");

        return {};
    }

    return ORStringNew(isolate, builder);
}

ModuleEntry *Importer::Lookup(ORString *key) {
    std::shared_lock guard(this->cache_lock_);

    ModuleMap::HEntry *hentry = nullptr;
    if (this->modules_.Lookup(key, &hentry) != LookupResult::OK)
        return nullptr;

    return hentry->value;
}

ModuleEntry *Importer::Insert(ORString *key, bool *was_inserted) {
    std::unique_lock guard(this->cache_lock_);

    ModuleMap::HEntry *hentry = nullptr;
    if (this->modules_.Lookup(key, &hentry) == LookupResult::OK) {
        if (was_inserted != nullptr)
            *was_inserted = false;

        return hentry->value;
    }

    // Confirmed miss — allocate.
    auto *entry = ModuleEntryNew(this->isolate_, key);
    if (entry == nullptr)
        return nullptr;

    hentry = this->modules_.AllocHEntry();
    if (hentry == nullptr) {
        guard.unlock();

        ModuleEntryDel(this->isolate_, entry);

        return nullptr;
    }

    hentry->key = key;
    hentry->value = entry;

    if (this->modules_.Insert(hentry) != LookupResult::OK) {
        this->modules_.FreeHEntry(hentry);

        guard.unlock();

        ModuleEntryDel(this->isolate_, entry);

        return nullptr;
    }

    O_FAST_INCREF(key);

    if (was_inserted != nullptr)
        *was_inserted = true;

    return entry;
}

void Importer::Prepare(ModuleEntry *entry, OObject *module, ImportSpec *spec) {
    assert(entry != nullptr && entry->state == ModuleState::LOADING);
    assert(entry->module == nullptr && entry->spec == nullptr);

    std::unique_lock guard(this->cache_lock_);

    entry->module = O_FAST_INCREF(module);
    entry->spec = O_FAST_INCREF(spec);
}

void Importer::PrepareCommit(ModuleEntry *entry, OObject *module, ImportSpec *spec) {
    assert(entry != nullptr && entry->state == ModuleState::LOADING);
    assert(entry->module == nullptr && entry->spec == nullptr);

    std::unique_lock guard(this->cache_lock_);

    entry->module = O_FAST_INCREF(module);
    entry->spec = O_FAST_INCREF(spec);

    entry->state = ModuleState::LOADED;
}

void Importer::Commit(ModuleEntry *entry) {
    assert(entry != nullptr && entry->state == ModuleState::LOADING);
    // module/spec must have been attached via `Prepare` before the
    // top-level ran — Commit only publishes the entry as LOADED.
    assert(entry->module != nullptr && entry->spec != nullptr);

    std::unique_lock guard(this->cache_lock_);

    entry->state = ModuleState::LOADED;
}

void Importer::Fail(ModuleEntry *entry) {
    if (entry == nullptr)
        return;

    std::unique_lock guard(this->cache_lock_);

    assert(entry->state == ModuleState::LOADING);

    ModuleMap::HEntry *hentry = nullptr;
    if (this->modules_.Remove(entry->name, &hentry) != LookupResult::OK)
        return;

    O_FAST_DECREF(hentry->key);

    this->modules_.FreeHEntry(hentry);

    guard.unlock();

    ModuleEntryDel(this->isolate_, entry);
}

HOObject orbiter::import::Import(Isolate *isolate, ORString *raw, const ImportSpec *origin) {
    auto *importer = isolate->importer_;

    // 1. Canonicalize: produce the absolute canonical cache key.
    const auto key = Canonicalize(isolate, raw, origin);
    if (!key)
        return {};

    // 2. Fast-path: an existing entry — LOADED or LOADING — short-circuits.
    //    For LOADING this is the same-fiber cycle case: returning
    //    `entry->module` exposes the partial module.
    if (const auto *existing = importer->Lookup(key.get()))
        return HOObject(existing->module);

    // 3. Miss → Insert. The re-check under unique lock inside Insert closes
    //    the race where another fiber inserted between our Lookup and the
    //    Insert.
    bool inserted = false;
    auto *entry = importer->Insert(key.get(), &inserted);
    if (entry == nullptr)
        return {};

    if (!inserted)
        return HOObject(entry->module);

    // 4. Resolve through the locator chain.
    Descriptor desc{};
    if (importer->Resolve(key.get(), &desc) != LocateResult::FOUND) {
        // NOT_MINE → ImportError(MODULE_NOT_FOUND) already set by Resolve.
        // ERROR    → a locator set the panic.
        importer->Fail(entry);

        return {};
    }

    // 5. Dispatch on the loader kind. BUILTIN and VIRTUAL share the
    //    "adopt a ready-made module" path; SOURCE and NATIVE are deliberate
    //    stubs until the loader subsystem lands.
    switch (desc.kind) {
        case LoaderKind::BUILTIN:
        case LoaderKind::VIRTUAL: {
            const auto spec = ImportSpecNew(isolate, key.get(), desc.origin, desc.locator,
                                            desc.kind, desc.is_package);
            if (!spec) {
                importer->Fail(entry);

                return {};
            }

            importer->PrepareCommit(entry, desc.module, spec.get());

            return HOObject(desc.module);
        }

        case LoaderKind::SOURCE: {
            ErrorSet(isolate,
                     ImportError::Details[ImportError::ID],
                     nullptr,
                     ImportError::Details[ImportError::LOADER_NOT_IMPLEMENTED],
                     "source",
                     ORSTRING_TO_CSTR(key.get()));

            importer->Fail(entry);

            return {};
        }

        case LoaderKind::NATIVE: {
            ErrorSet(isolate,
                     ImportError::Details[ImportError::ID],
                     nullptr,
                     ImportError::Details[ImportError::LOADER_NOT_IMPLEMENTED],
                     "native",
                     ORSTRING_TO_CSTR(key.get()));

            importer->Fail(entry);

            return {};
        }
    }

    // Unreachable — the switch covers every LoaderKind. Defensive cleanup
    // so a future enum addition doesn't leak a LOADING entry by accident.
    importer->Fail(entry);

    return {};
}
