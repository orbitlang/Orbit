// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <cctype>
#include <cstring>
#include <shared_mutex>

#include <orbit/liftoff/compiler.h>
#include <orbit/liftoff/olevel.h>

#include <orbit/orbiter/fiber.h>
#include <orbit/orbiter/isolate.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/orbiter/import/importer.h>

using namespace orbiter::datatype;
using namespace orbiter::import;

AcquireOutcome Importer::Acquire(ORString *key, ModuleEntry * &out) noexcept {
    auto *me = Fiber::Current();

    ModuleMap::HEntry *hentry = nullptr;

    // First: is the key already in the cache?
    if (this->modules_.Lookup(key, &hentry) == LookupResult::OK) {
        out = hentry->value;

        if (out->state == ModuleState::LOADED)
            return AcquireOutcome::LOADED;

        // LOADING: distinguish same-fiber cycle (return partial) from
        // cross-fiber concurrent load (block, after cycle check).
        assert(out->state == ModuleState::LOADING);

        if (out->owner == me)
            return AcquireOutcome::PARTIAL;

        // Cross-fiber LOADING. If blocking here would close a wait-for
        // cycle, the calling fiber takes the partial module instead of
        // blocking. `out` already points at the target entry, so
        // PARTIAL hands back `out->module`, which is non-null: a cycle can
        // only be detected once the chain's owners exist, i.e. after their
        // executors were spawned and the modules `Prepare`'d.
        if (this->HasCycle(me, out))
            return AcquireOutcome::PARTIAL;

        return this->EnqueueAndWait(me, out);
    }

    // Miss — insert a fresh LOADING entry owned by the calling fiber.
    out = this->Insert(key);
    if (out == nullptr)
        return AcquireOutcome::ERROR;

    return AcquireOutcome::FRESH;
}

AcquireOutcome Importer::EnqueueAndWait(Fiber *me, ModuleEntry *entry) noexcept {
    auto *we = this->wait_for_.AllocHEntry();
    if (we == nullptr)
        return AcquireOutcome::ERROR;

    we->key = me;
    we->value = entry;

    if (this->wait_for_.Insert(we) != LookupResult::OK) {
        this->wait_for_.FreeHEntry(we);

        return AcquireOutcome::ERROR;
    }

    entry->waiters.Enqueue(me);

    return AcquireOutcome::BLOCKED;
}

bool Importer::BlockOnExecutor(ModuleEntry *entry, Fiber *executor) noexcept {
    std::unique_lock guard(this->cache_lock_);

    // The executor is the fiber that will run the top-level and eventually
    // Commit/Fail this entry — that makes it the entry's `owner` for the
    // purposes of same-fiber detection and wait-for cycle walking.
    entry->owner = executor;

    // The importing fiber now waits for the executor: enqueue it on the
    // entry and register its wait-for edge.
    return this->EnqueueAndWait(Fiber::Current(), entry) == AcquireOutcome::BLOCKED;
}

bool Importer::HasCycle(const Fiber *me, const ModuleEntry *target) const {
    // Walk owner → blocked-on → owner ... from `target`'s owner, looking
    // for `me` in the chain. If we find it, blocking `me` on `target`
    // would close a cross-fiber circular import.
    auto *cur = target->owner;
    while (cur != nullptr) {
        if (cur == me)
            return true;

        WaitForMap::HEntry *we = nullptr;
        if (this->wait_for_.Lookup(cur, &we) != LookupResult::OK)
            return false; // `cur` isn't blocked → chain ends, no cycle

        cur = we->value->owner;
    }

    return false;
}

static bool IsHostPathSep(const char c) noexcept {
#ifdef _ORBIT_PLATFORM_WINDOWS
    return c == '/' || c == '\\';
#else
    return c == _ORBIT_PLATFORM_PATHSEP[0];
#endif
}

HModule Importer::LoadScriptSource(ORString *key, const Descriptor &desc, HCode &code, ModuleEntry *entry) {
    const auto last_sep = ORStringRFind(key, kPathSep);
    const auto *r_name = ORSTRING_TO_CSTR(key) + last_sep + 1;

    const auto name = ORStringIntern(this->isolate_, r_name);
    if (!name) {
        this->Fail(entry);

        return {};
    }

    auto *source = fopen(ORSTRING_TO_CSTR(desc.origin), "r");
    if (source == nullptr) {
        // TODO: set errno panic here

        this->Fail(entry);

        return {};
    }

    // TODO: load optimization levels from Orbit configuration
    liftoff::Compiler compiler(this->isolate_, liftoff::kDefaultOptimization, true);

    code = compiler.Compile(r_name, source);
    if (!code) {
        this->Fail(entry);

        return {};
    }

    const auto module_type = ModuleTypeNew(code.get(), name.get());
    if (!module_type) {
        this->Fail(entry);

        return {};
    }

    auto module = ModuleNew(module_type.get());
    if (!module) {
        this->Fail(entry);

        return {};
    }

    const auto spec = ImportSpecNew(this->isolate_, key, desc.origin, desc.locator, desc.kind, true);
    if (!spec) {
        this->Fail(entry);

        return {};
    }

    this->Prepare(entry, module.get(), spec.get());

    const auto prop = TIFindLocalProperty(module_type.get(), "__spec__");
    assert(prop != nullptr);

    prop->value = (OObject *) O_INCREF(spec.get());

    return module;
}

static HORString JoinPathRaw(orbiter::Isolate *isolate, const char *base, const MSize base_len, const char *leaf,
                             const MSize leaf_len) {
    // Either side empty: trivially the other side.
    if (base_len == 0)
        return ORStringNew(isolate, leaf, leaf_len);

    if (leaf_len == 0)
        return ORStringNew(isolate, base, base_len);

    const bool base_has_sep = IsHostPathSep(base[base_len - 1]);
    const bool leaf_has_sep = IsHostPathSep(leaf[0]);

    // Collapse: skip the leading separator of `leaf` so the junction
    // ends up with exactly one separator instead of two.
    if (base_has_sep && leaf_has_sep)
        return ORStringFormat(isolate, "%.*s%s", (int) base_len, base, leaf + 1);

    // Exactly one separator at the junction already — straight concat.
    if (base_has_sep || leaf_has_sep)
        return ORStringFormat(isolate, "%.*s%s", (int) base_len, base, leaf);

    // Neither side has a separator at the junction — insert the host one.
    return ORStringFormat(isolate, "%.*s%s%s", (int) base_len, base, kHostPathSep, leaf);
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

orbiter::import::ModuleEntry *Importer::Insert(ORString *key) {
    ModuleMap::HEntry *hentry = nullptr;

    auto *entry = ModuleEntryNew(this->isolate_, key);
    if (entry == nullptr)
        return nullptr;

    hentry = this->modules_.AllocHEntry();
    if (hentry == nullptr) {
        ModuleEntryDel(this->isolate_, entry);

        return nullptr;
    }

    hentry->key = key;
    hentry->value = entry;

    if (this->modules_.Insert(hentry) != LookupResult::OK) {
        this->modules_.FreeHEntry(hentry);

        ModuleEntryDel(this->isolate_, entry);

        return nullptr;
    }

    O_FAST_INCREF(key);

    return entry;
}

void Importer::Prepare(ModuleEntry *entry, Module *module, ImportSpec *spec) {
    assert(entry != nullptr && entry->state == ModuleState::LOADING);
    assert(entry->module == nullptr && entry->spec == nullptr);

    std::unique_lock guard(this->cache_lock_);

    entry->module = O_FAST_INCREF(module);
    entry->spec = O_FAST_INCREF(spec);
}

void Importer::PrepareCommit(ModuleEntry *entry, Module *module, ImportSpec *spec) {
    assert(entry != nullptr && entry->state == ModuleState::LOADING);
    assert(entry->module == nullptr && entry->spec == nullptr);

    std::unique_lock guard(this->cache_lock_);

    entry->module = O_FAST_INCREF(module);
    entry->spec = O_FAST_INCREF(spec);

    entry->state = ModuleState::LOADED;
}

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

bool Importer::AddRoot(const char *path) const {
    // Reject empty input up front so we don't allocate an ORString just to
    // discard it in the validation below.
    if (path == nullptr || *path == '\0')
        return false;

    const auto root = ORStringNew(this->isolate_, path);
    if (!root)
        return false;

    return this->AddRoot(root.get());
}

bool Importer::AddRoot(ORString *path) const {
    const auto len = ORSTRING_LENGTH(path);
    if (len == 0)
        return false;

    const auto *buf = ORSTRING_TO_CSTR(path);
    const char last = buf[len - 1];

#ifdef _ORBIT_PLATFORM_WINDOWS
    // Windows accepts both '/' and '\\' interchangeably at the OS level.
    if (last == '/' || last == '\\')
        return ListAppend(this->roots_.get(), (OObject *) path);
#else
    if (last == _ORBIT_PLATFORM_PATHSEP[0])
        return ListAppend(this->roots_.get(), (OObject *) path);
#endif

    const auto normalized = ORStringFormat(this->isolate_, "%s%s", buf, _ORBIT_PLATFORM_PATHSEP);
    if (!normalized)
        return false;

    return ListAppend(this->roots_.get(), (OObject *) normalized.get());
}

void Importer::Commit(ModuleEntry *entry) {
    assert(entry != nullptr && entry->state == ModuleState::LOADING);
    assert(entry->module != nullptr && entry->spec != nullptr);

    std::unique_lock guard(this->cache_lock_);

    entry->state = ModuleState::LOADED;
    entry->owner = nullptr;

    auto *cursor = entry->waiters.Dequeue();
    while (cursor != nullptr) {
        // The waiter is no longer blocked on us — clear its wait-for edge
        // so it doesn't sit stale in the graph.
        WaitForMap::HEntry *we = nullptr;
        if (this->wait_for_.Remove(cursor, &we) == LookupResult::OK)
            this->wait_for_.FreeHEntry(we);

        Orbiter::GetInstance()->PushFiber(cursor);
        cursor = entry->waiters.Dequeue();
    }
}

void Importer::Fail(ModuleEntry *entry) {
    if (entry == nullptr)
        return;

    std::unique_lock guard(this->cache_lock_);

    assert(entry->state == ModuleState::LOADING);

    entry->state = ModuleState::FAILED;

    const auto *self = Fiber::Current();
    auto *cursor = entry->waiters.Dequeue();
    while (cursor != nullptr) {
        // Same as Commit: clear the waiter's wait-for edge before waking it
        WaitForMap::HEntry *we = nullptr;
        if (this->wait_for_.Remove(cursor, &we) == LookupResult::OK)
            this->wait_for_.FreeHEntry(we);

        cursor->Panic(self->GetPanicError().get());

        Orbiter::GetInstance()->PushFiber(cursor);
        cursor = entry->waiters.Dequeue();
    }

    ModuleMap::HEntry *hentry = nullptr;
    if (this->modules_.Remove(entry->name, &hentry) != LookupResult::OK)
        return;

    O_FAST_DECREF(hentry->key);

    this->modules_.FreeHEntry(hentry);

    guard.unlock();

    ModuleEntryDel(this->isolate_, entry);
}

ImportStatus Importer::Import(ORString *raw, const ImportSpec *origin, Module *&out_module) noexcept {
    // 1. Canonicalize: produce the absolute canonical cache key.
    const auto key = Canonicalize(this->isolate_, raw, origin);
    if (!key)
        return ImportStatus::ERROR;

    std::unique_lock lock(this->cache_lock_);

    ModuleEntry *entry = nullptr;
    switch (this->Acquire(key.get(), entry)) {
        case AcquireOutcome::LOADED:
        case AcquireOutcome::PARTIAL:
            out_module = entry->module;
            return ImportStatus::OK;

        case AcquireOutcome::BLOCKED:
            return ImportStatus::BLOCKED;

        case AcquireOutcome::ERROR:
            return ImportStatus::ERROR;

        case AcquireOutcome::FRESH:
            break; // fall through to load
    }

    lock.unlock();

    // 3. Resolve via the locator chain.
    Descriptor desc{};
    if (this->Resolve(key.get(), &desc) != LocateResult::FOUND) {
        this->Fail(entry);

        return ImportStatus::ERROR;
    }

    // 4. Dispatch on the loader kind.
    switch (desc.kind) {
        case LoaderKind::BUILTIN:
        case LoaderKind::VIRTUAL: {
            const auto spec = ImportSpecNew(this->isolate_, key.get(), desc.origin, desc.locator, desc.kind,
                                            desc.is_package);
            if (!spec) {
                this->Fail(entry);

                return ImportStatus::ERROR;
            }

            auto *prop = TIFindLocalProperty(O_GET_TYPE(desc.module), "__spec__");

            assert(prop != nullptr);

            prop->value = (OObject *) O_INCREF(spec.get());

            this->PrepareCommit(entry, desc.module, spec.get());

            out_module = desc.module;

            return ImportStatus::OK;
        }

        case LoaderKind::SOURCE: {
            HCode code;
            const auto mod = this->LoadScriptSource(key.get(), desc, code, entry);
            if (!mod)
                return ImportStatus::ERROR; // LoadScriptSource already Fail'd

            auto *orbiter = Orbiter::GetInstance();
            const auto *self = Fiber::Current();

            assert(orbiter != nullptr);
            assert(self != nullptr);

            auto *executor = Orbiter::EvalDetached(self->context.context, mod.get(), code.get());

            if (!this->BlockOnExecutor(entry, executor)) {
                Orbiter::DiscardDetachedFiber(executor);

                this->Fail(entry);

                return ImportStatus::ERROR;
            }

            executor->module_entry = entry;

            orbiter->PushFiber(executor);

            return ImportStatus::BLOCKED;
        }

        case LoaderKind::NATIVE: {
            ErrorSet(this->isolate_,
                     ImportError::Details[ImportError::ID],
                     nullptr,
                     ImportError::Details[ImportError::LOADER_NOT_IMPLEMENTED],
                     "native",
                     ORSTRING_TO_CSTR(key.get()));

            this->Fail(entry);

            return ImportStatus::ERROR;
        }
    }

    // Unreachable — the switch covers every LoaderKind. Defensive cleanup
    // so a future enum addition doesn't leak a LOADING entry by accident.
    this->Fail(entry);

    return ImportStatus::ERROR;
}

Module *Importer::Import(ORString *name, const ImportSpec *origin) noexcept {
    Module *out_module = nullptr;

    const auto status = this->Import(name, origin, out_module);

    if (status == ImportStatus::OK)
        return out_module;

    return nullptr;
}

Module *Importer::Import(const char *name, const ImportSpec *origin) noexcept {
    const auto oname = ORStringNew(this->isolate_, name);
    if (!oname)
        return nullptr;

    return this->Import(oname.get(), origin);
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

HORString orbiter::import::JoinPath(Isolate *isolate, const ORString *base, const ORString *leaf) {
    if (base == nullptr || leaf == nullptr)
        return {};

    return JoinPathRaw(isolate,ORSTRING_TO_CSTR(base), ORSTRING_LENGTH(base),
                       ORSTRING_TO_CSTR(leaf), ORSTRING_LENGTH(leaf));
}

HORString orbiter::import::JoinPath(Isolate *isolate, const ORString *base, const char *leaf) {
    if (base == nullptr || leaf == nullptr)
        return {};

    return JoinPathRaw(isolate,ORSTRING_TO_CSTR(base), ORSTRING_LENGTH(base),
                       leaf, (MSize) strlen(leaf));
}

ImportStatus orbiter::import::Import(const Isolate *isolate, ORString *raw, const ImportSpec *origin,
                                     Module * &out_module) {
    return isolate->importer_->Import(raw, origin, out_module);
}

ImportStatus orbiter::import::Import(const Isolate *isolate, ORString *raw, const Module *base, Module * &out_module) {
    const PropertyDescriptor *prop = nullptr;

    if (base != nullptr)
        prop = TIFindLocalProperty(O_GET_TYPE(base), "__spec__");

    return isolate->importer_->Import(raw, (ImportSpec *) (prop != nullptr ? prop->value : nullptr), out_module);
}
