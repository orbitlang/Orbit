// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/import/importspec.h>

using namespace orbiter::datatype;
using namespace orbiter::import;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

/// Human-readable tag for the loader kind, used by `str(spec)` and exposed as
/// the diagnostic name of the loader (never a callable — see import/README.md).
static const char *LoaderKindName(const LoaderKind kind) {
    switch (kind) {
        case LoaderKind::BUILTIN: return "builtin";
        case LoaderKind::SOURCE: return "source";
        case LoaderKind::NATIVE: return "native";
        case LoaderKind::VIRTUAL: return "virtual";
    }

    return "unknown";
}

// *********************************************************************************************************************
// PROPERTIES
// *********************************************************************************************************************

// The five trailing pointer slots. Read-only and public — an ImportSpec is immutable once a module is committed.
const OPropertyEntry importspec_props[] = {
    OPROPERTY_ENTRY("name", 0, PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("origin", 1, PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("locator", 2, PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("loader", 3, PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC),
    OPROPERTY_ENTRY("is_package", 4, PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC),

    OPROPERTY_SENTINEL
};

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Each loaded module owns exactly one spec — identity equality only.
static bool ImportSpecEqual(const OObject *left, const OObject *right, bool &out) {
    out = left == right;

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// `str(spec)` / `repr(spec)`: `module-spec '<name>' (<loader>) at '<origin>'`.
static OObject *ImportSpecToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto *spec = (const ImportSpec *) self;

    const auto s = ORStringFormat(isolate, "module-spec '%s' (%s) at '%s'",
                                  ORSTRING_TO_CSTR(spec->name),
                                  LoaderKindName(spec->Loader()),
                                  ORSTRING_TO_CSTR(spec->origin));

    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::import::ImportSpecTypeSetup(TypeInfo *self) {
    // name/origin/locator live in the trailing three slots; the GC traces
    // slot-held references automatically, so no destructor or trace callback
    // is needed.
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = ImportSpecEqual;
    ops.to_string = ImportSpecToString;

    return TIPropertyAdd(self, importspec_props);
}

HImportSpec orbiter::import::ImportSpecNew(Isolate *isolate, ORString *name, ORString *origin, OObject *locator,
                                           const LoaderKind loader, const bool is_package) {
    auto *spec = MakeObject<ImportSpec>(isolate, InstanceType::IMPORT_SPEC);
    if (spec != nullptr) {
        spec->name = name;
        spec->origin = origin;
        spec->locator = locator;

        spec->loader = (OObject *) O_TO_SMI((MSSize) loader);
        spec->is_package = (OObject *) BOOL_TO_OBOOL(is_package);
    }

    O_GC_TRACK_RETURN(isolate, spec, true);
}

HOType orbiter::import::ImportSpecTypeInit(Isolate *isolate) {
    return MakeType(isolate, "ImportSpec", InstanceType::IMPORT_SPEC, 0, 5, 5);
}
