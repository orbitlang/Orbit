// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/rawptr.h>

#include <orbit/orbiter/native/loader.h>

using namespace orbiter::datatype;
using namespace orbiter::native;

bool Loader::Initialize() {
    this->cache_ = DictNew(this->isolate_);
    if (!this->cache_)
        return false;

    this->lib_cache_ = DictNew(this->isolate_);
    if (!this->lib_cache_) {
        this->cache_.reset();

        return false;
    }

    return true;
}

DLHandle Loader::LoadLibrary(ORString *name, bool &closable) {
    const char *library = nullptr;

    closable = false;

    std::unique_lock _(this->lib_cache_mutex_);

    if (name != nullptr) {
        library = ORSTRING_TO_CSTR(name);

        HOObject out_value_;
        if (DictLookup(this->lib_cache_.get(), (OObject *) name, out_value_))
            return ((RawPtr *) out_value_.get())->ptr;
    }

    const auto lib = OpenLibrary(this->isolate_, library);
    if (lib == DLHandleError)
        return nullptr;

    const auto ret = RawPtrNew(this->isolate_, lib);
    if (!ret)
        return nullptr;

    if (name != nullptr)
        DictInsert(this->lib_cache_.get(), (OObject *) name, (OObject *) ret.get());

    closable = true;

    return ret->ptr;
}

HOObject Loader::Load(NativeBinding *binding)  {
    const auto key = this->MakeKey(binding->library, binding->symbol, binding->binding_type);

    HOObject out_value_;
    if (DictLookup(this->cache_.get(), (OObject *) key.get(), out_value_))
        return out_value_;

    auto ret = binding->binding_type == NativeBindingType::FUNC
                   ? LoadFunction(binding)
                   : LoadVariable(binding);

    DictInsert(this->cache_.get(), (OObject *) key.get(), ret.get());

    return ret;
}

HOObject Loader::LoadFunction(NativeBinding *binding) {
    bool closable = false;

    std::unique_lock _(this->cache_mutex_);

    const DLHandle lib = this->LoadLibrary(binding->library, closable);
    if (lib == DLHandleError)
        return {};

    auto *func = LoadSymbol(this->isolate_, lib, ORSTRING_TO_CSTR(binding->symbol));
    if (func == DLHandleError) {
        this->CloseLibrary(lib, binding->library, closable);

        return {};
    }

    const auto native_func = NativeFuncNew(this->isolate_, binding, func);
    return HOObject((OObject *) native_func.get());
}

HOObject Loader::LoadVariable(NativeBinding *binding)  {
    assert(false);

    return {};
}

HORString Loader::MakeKey(const ORString *library, const ORString *symbol, const NativeBindingType type) const {
    const char *l_str = library != nullptr ? ORSTRING_TO_CSTR(library) : "";
    const auto s_str = ORSTRING_TO_CSTR(symbol);

    char sym_type[2]{};
    if (type == NativeBindingType::FUNC)
        sym_type[0] = 'F';
    else
        sym_type[0] = 'V';

    return ORStringFormat(this->isolate_, "%s:%s:%s", l_str, s_str, sym_type);
}

void Loader::CloseLibrary(DLHandle handle, ORString *name, const bool closable) const {
    if (!closable)
        return;

    ::CloseLibrary(this->isolate_, handle, ORSTRING_TO_CSTR(name));

    DictRemove(this->lib_cache_.get(), (OObject *) name);
}
