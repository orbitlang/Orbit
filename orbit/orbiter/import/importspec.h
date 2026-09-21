// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_IMPORT_IMPORTSPEC_H_
#define ORBIT_ORBITER_IMPORT_IMPORTSPEC_H_

#include <orbit/orbiter/datatype/orstring.h>

namespace orbiter::import {
    using namespace orbiter::datatype;

    /**
     * @brief Which loader produced a module — also selects the loading strategy.
     *
     * Set on the resolved `Descriptor` by the locator chain and copied onto the
     * module's `ImportSpec`. See `import/README.md`.
     *
     * BUILTIN  — an internal `::orbit::*` primitive. The module is the
     *            pre-registered object; first use runs a one-shot init, no file
     *            is executed.
     * SOURCE   — an on-disk `.orb` file: compiled and executed.
     * NATIVE   — an on-disk shared object (`.so` / `.dll`). Reserved; not yet
     *            wired.
     * VIRTUAL  — produced by a runtime-registered locator: either an in-memory
     *            source buffer to compile, or a ready-made module object adopted
     *            as-is (no execution).
     */
    enum class LoaderKind : U8 {
        BUILTIN,
        SOURCE,
        NATIVE,
        VIRTUAL,
    };

    /**
     * @brief The public, mostly-immutable identity of a loaded module.
     *
     * Every loaded module carries one of these (exposed to user code as
     * `__spec`). It is a pure data-holder: it describes *what* a module is and
     * *where it came from*.
     *
     * Fields (all five are the object's trailing pointer slots):
     *   - `name`        the canonical, absolute, OS-independent import key
     *                   (e.g. `"a/b/c"` or `"::orbit::io"`). This is also the
     *                   registry cache key. The base for this module's
     *                   relative (`./`) imports is derived from it on demand
     *                   (`dirname(name)`, or `name` itself for a directory
     *                   package), never stored twice.
     *   - `origin`      absolute on-disk path of the loaded file, or a synthetic
     *                   marker for builtin / virtual modules.
     *   - `locator`     for `VIRTUAL` modules only: the locator handle (as
     *                   returned by `register_locator`) that produced this
     *                   module, so the exact custom loader is identifiable.
     *                   `nullptr` for `BUILTIN` / `SOURCE` / `NATIVE`, where
     *                   `loader` already fully identifies the provenance.
     *   - `loader`      which loader produced the module — stored as an SMI of
     *                   the `LoaderKind` value; use `Loader()`.
     *   - `is_package`  true when resolved via the directory-as-package form
     *                   (`name/name.orb`) rather than a plain `name.orb` file —
     *                   stored as an Orbit BOOL; use `IsPackage()`.
     */
    struct ImportSpec {
        OROBJ_HEAD;

        /// Canonical absolute import key. Also the registry cache key.
        /// Strong reference, traced by the GC.
        ORString *name;

        /// Absolute on-disk path, or a synthetic marker for builtin/virtual.
        /// Strong reference, traced by the GC.
        ORString *origin;

        /// For VIRTUAL modules: the locator handle that produced this module.
        /// nullptr for BUILTIN/SOURCE/NATIVE.
        OObject *locator;

        /// Loader that produced this module, as an SMI of `LoaderKind`.
        /// Read it through `Loader()` rather than decoding by hand.
        OObject *loader;

        /// Directory-package flag (`name/name.orb`), as an Orbit BOOL.
        /// Read it through `IsPackage()`.
        OObject *is_package;

        /// Decoded loader kind. The on-slot representation is an SMI so the
        /// value is exposed to Orbit as a plain integer.
        [[nodiscard]] LoaderKind Loader() const {
            return (LoaderKind) O_FROM_SMI(this->loader);
        }

        /// Decoded directory-package flag. The on-slot representation is an
        /// Orbit BOOL so it is exposed to Orbit as a plain boolean.
        [[nodiscard]] bool IsPackage() const {
            return O_IS_TRUE(this->is_package);
        }
    };

    using HImportSpec = Handle<ImportSpec>;

    /**
     * @brief Set up additional features and properties for the specified type
     *
     * This function enriches the previously created type with various functionalities.
     * It typically performs the following tasks:
     * - Adds default methods to the type
     * - Adds required properties to the type
     *
     * This function is called immediately after the type's Init function to complete its setup.
     *
     * @param self Pointer to TypeInfo created by %type%Init call
     *
     * @return true if setup was successful, false otherwise
     */
    bool ImportSpecTypeSetup(TypeInfo *self);

    /**
     * @brief Allocate a new ImportSpec.
     *
     * The returned spec is fully populated and immutable thereafter; callers
     * (the loaders) build it once when a module is committed to the registry.
     *
     * @param isolate     Owning isolate.
     * @param name        Canonical absolute import key / registry cache key.
     *                    A strong reference is kept; the GC traces it.
     * @param origin      Absolute on-disk path, or a synthetic marker for
     *                    builtin / virtual modules. Strong reference, traced.
     * @param locator     For VIRTUAL modules, the locator handle that produced
     *                    it; pass nullptr for BUILTIN / SOURCE / NATIVE. A
     *                    strong reference is kept; the GC traces it when set.
     * @param loader      Loader that produced the module.
     * @param is_package  True when resolved as a directory-package.
     *
     * @return Handle to the new ImportSpec, or an empty handle on allocation
     *         failure.
     */
    HImportSpec ImportSpecNew(Isolate *isolate, ORString *name, ORString *origin, OObject *locator, LoaderKind loader,
                              bool is_package);

    /**
     * @brief Initialize and create the specified type
     *
     * This function creates a new TypeInfo object representing the specific type.
     * It sets up the basic structure and core properties of the type.
     *
     * @param isolate Pointer to the Isolate in which the type is being created
     *
     * @return Handle to the newly created TypeInfo for the type, or an empty handle if creation failed
     */
    HOType ImportSpecTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_IMPORT_IMPORTSPEC_H_
