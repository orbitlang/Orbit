// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <shared_mutex>

#include <sys/stat.h>

#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/module/modules.h>

#include <orbit/orbiter/import/importer.h>
#include <orbit/orbiter/import/locator.h>

using namespace orbiter::datatype;
using namespace orbiter::import;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

/// Fixed builtin table.
static const ModuleInit *const kBuiltins[] = {
    orbiter::module::module_builtin_,
    orbiter::module::module_io_
};

/// True if @p path names an existing regular file.
static bool IsRegularFile(const char *path) {
#ifdef _ORBIT_PLATFORM_WINDOWS
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
    struct stat st{};

    return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/// Builds an absolute candidate path and, if it is a regular file, fills @p out
/// as a SOURCE descriptor and returns true.
static bool TryCandidate(const HORString &path, const bool is_package, Descriptor *out) {
    if (!IsRegularFile(ORSTRING_TO_CSTR(path.get())))
        return false;

    out->kind = LoaderKind::SOURCE;
    out->origin = path.get();
    out->is_package = is_package;
    out->module = nullptr;
    out->source = nullptr;
    out->locator = nullptr;

    return true;
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

LocateResult orbiter::import::BuiltinLocate(const Importer *importer, const ORString *key, Descriptor *out) {
    const auto *kbuf = ORSTRING_TO_CSTR(key);
    const auto klen = ORSTRING_LENGTH(key);

    // Builtin namespace only: a non-`::` key is never ours.
    if (klen < 3 || kbuf[0] != ':' || kbuf[1] != ':')
        return LocateResult::NOT_MINE;

    for (const auto *cursor: kBuiltins) {
        const auto nlen = strlen(cursor->name);
        if (klen != nlen || memory::MemoryCompare(kbuf, cursor->name, nlen) != 0)
            continue;

        const auto module_type = ModuleTypeNew(importer->GetIsolate(), cursor);
        if (!module_type)
            return LocateResult::ERROR;

        const auto module = ModuleNew(module_type.get());
        if (!module)
            return LocateResult::ERROR;

        // Modules can request a runtime init pass — needed when entries in
        // the static `bulk` table cannot be filled with constant values at
        // C++ initialization time (e.g. per-isolate `TypeInfo *`).  The
        // callback receives the freshly-built module instance and is
        // expected to patch the placeholder slots with their real values.
        if (cursor->init != nullptr && !cursor->init(module.get()))
            return LocateResult::ERROR;

        out->kind = LoaderKind::BUILTIN;
        out->origin = (ORString *) key;
        out->is_package = false;
        out->module = module.get();
        out->source = nullptr;
        out->locator = nullptr;

        return LocateResult::FOUND;
    }

    return LocateResult::NOT_MINE;
}

LocateResult orbiter::import::FsSourceLocate(const Importer *importer, const ORString *key, Descriptor *out) {
    const auto *kbuf = ORSTRING_TO_CSTR(key);
    const auto klen = ORSTRING_LENGTH(key);

    // `::`-prefixed keys are the builtin namespace — not the filesystem's.
    if (klen >= 2 && kbuf[0] == ':' && kbuf[1] == ':')
        return LocateResult::NOT_MINE;

    auto *roots = importer->Roots();
    auto *isolate = importer->GetIsolate();

    std::shared_lock _(roots->lock);

    for (const auto ext: kExtension) {
        for (MSize i = 0; i < roots->length; i++) {
            const auto *entry = (ORString *) roots->objects[i];
            if (!O_IS_OBJECT(entry) || !O_IS_TYPE(entry, InstanceType::STRING))
                continue;

            // <root>/<key>.ext — a plain file module.
            // import "a" -> <root>/a.ext
            // import "a/b/c" -> <root>/a/b/c.ext
            auto candidate = ORStringFormat(isolate, "%s%s%s", ORSTRING_TO_CSTR(entry), kbuf, ext);
            if (!candidate)
                return LocateResult::ERROR;

            if (TryCandidate(candidate, false, out))
                return LocateResult::FOUND;

            // <root>/<key>/<key>.ext — the directory-as-package form.
            // import "a" -> <root>/a/a.ext
            // import "a/b/c" -> <root>/a/b/c/c.ext
            auto base = kbuf;
            const auto last_sep = ORStringRFind(key, kPathSep);
            if (last_sep >= 0)
                base += last_sep + 1;

            candidate = ORStringFormat(isolate, "%s%s%s%s%s", ORSTRING_TO_CSTR(entry), kbuf, kPathSep, base, ext);
            if (!candidate)
                return LocateResult::ERROR;

            if (TryCandidate(candidate, true, out))
                return LocateResult::FOUND;
        }
    }

    return LocateResult::NOT_MINE;
}
