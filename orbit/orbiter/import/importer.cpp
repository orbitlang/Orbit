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
