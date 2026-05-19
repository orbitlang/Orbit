// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_IMPORT_LOCATOR_H_
#define ORBIT_ORBITER_IMPORT_LOCATOR_H_

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/import/importspec.h>

namespace orbiter::import {
    using namespace orbiter::datatype;

    /**
     * @brief Tri-state outcome of a single locator.
     *
     * The tri-state is what keeps a broken-but-mine module from being masked as
     * "module not found" — see import/README.md.
     *
     * NOT_MINE — the locator does not handle this key; the chain continues with
     *            the next locator.
     * FOUND    — resolved; the `Descriptor` out-param is filled. The chain stops
     *            and hands the descriptor to the matching loader.
     * ERROR    — the key *is* handled by this locator but could not be
     *            delivered (e.g. unreadable file). The chain stops and the
     *            isolate panic state is set; the caller propagates it. Never
     *            fall through to the next locator.
     */
    enum class LocateResult : U8 {
        NOT_MINE,
        FOUND,
        ERROR,
    };

    /// @brief What a locator resolved a key to — consumed by the loaders.
    struct Descriptor {
        /// Absolute on-disk path (SOURCE/NATIVE) or a synthetic label
        /// (BUILTIN/VIRTUAL). Becomes `ImportSpec::origin`.
        ORString *origin;

        /// VIRTUAL only: a ready-made module adopted as-is. null otherwise.
        OObject *module;

        /// VIRTUAL only: an in-memory source buffer to compile. null otherwise.
        OObject *source;

        /// VIRTUAL only: the locator handle that produced this, for provenance
        /// (becomes `ImportSpec::locator`). null otherwise.
        OObject *locator;

        /// Selects the loader and the interpretation of the other fields.
        LoaderKind kind;

        /// True only for the directory-as-package form (`key/<base>.orb`).
        bool is_package;
    };

    /**
     * @brief A single locator body.
     *
     * @param isolate  Owning isolate (allocation and, on ERROR, panic).
     * @param key      The canonical, absolute, OS-independent import key.
     * @param out      Filled only on FOUND.
     *
     * @return The tri-state outcome. On ERROR the isolate panic is set.
     */
    using LocateFn = LocateResult (*)(Isolate *isolate, const ORString *key, Descriptor *out);

    /// @brief One entry in the locator chain.
    struct Locator {
        /// Diagnostic name ("builtin", "fs-source", or a user locator name).
        const char *name;

        /// The locator body.
        LocateFn locate;
    };

    /**
     * @brief The fs-source locator body.
     *
     * Probes the isolate's import roots for an on-disk `.orb` module. For each
     * root, in order, tries `<root>/<key>.orb` then
     * `<root>/<key>/<basename(key)>.orb` (the directory-as-package form, which
     * sets `Descriptor::is_package`). `::`-prefixed keys belong to the builtin
     * namespace and are declined immediately.
     *
     * @note First cut: only "regular file exists" is treated as a hit; any
     *       other stat outcome (including permission errors) currently yields
     *       NOT_MINE. Distinguishing "mine but unreadable" → ERROR is a
     *       deliberate follow-up.
     */
    LocateResult FsSourceLocate(const Importer *importer, const ORString *key, Descriptor *out);
}

#endif // !ORBIT_ORBITER_IMPORT_LOCATOR_H_
