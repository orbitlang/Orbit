// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/stringbuilder.h>

#include <orbit/orbiter/import/importer.h>

using namespace orbiter::datatype;
using namespace orbiter::import;

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool Importer::Initialize() {
    this->roots_ = ListNew(this->isolate_);

    return (bool) this->roots_;
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
