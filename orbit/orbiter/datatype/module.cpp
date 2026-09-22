// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/datatype/module.h>

using namespace orbiter::datatype;

static bool ModuleAUXDtor(TypeInfo *self) {
    const orbiter::memory::IsolateAllocator allocator(self->isolate);

    allocator.free(self->aux.data);

    self->aux.data = nullptr;

    return true;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// `str(module)` / `repr(module)`: produces `<module 'name'>`
static OObject *ModuleToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto s = ORStringFormat(isolate, "<module '%s'>", O_GET_TYPE(self)->name);
    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::ModuleSetLocalProperty(const TypeInfo *self, const char *name, OObject *value) {
    if (name == nullptr) {
        if (!O_IS_OBJECT(value))
            return false;

        const auto *type = GetTypeInfoFromObject(self->isolate, value);
        name = type->name;
    }

    auto prop = TIFindLocalProperty(self, name);
    if (prop != nullptr) {
        auto *old = prop->value;

        prop->value = O_INCREF(value);

        O_DECREF(old);

        return true;
    }

    return false;
}

bool orbiter::datatype::ModuleTypeSetup(TypeInfo *self) {
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.to_string = ModuleToString;

    return true;
}

HModule orbiter::datatype::ModuleNew(TypeInfo *tp_module) {
    auto *isolate = O_GET_ISOLATE(tp_module);

    assert(tp_module->i_type == InstanceType::MODULE);

    auto *module = MakeObject<Module>(tp_module);
    if (module == nullptr)
        return {};

    memory::MemoryZero(((unsigned char *) module) + tp_module->offset + tp_module->headroom,
                       tp_module->i_size - (tp_module->offset + tp_module->headroom));

    // The handle is taken before anything else can allocate: a tracked object
    // is only a root while its refcount is above zero, and this one starts at
    // zero.
    auto handle = HModule(module);

    isolate->gc->Track((OObject *) module, true);

    // Native module functions are built here, not while the module type is
    // described, because a function needs the module *instance* to reach its
    // state at call time, and the instance only exists now.
    //
    // Each function is handed to its slot without keeping a reference: the
    // module is a traced container and a root, so its slots are what keeps them
    // alive. No write barrier is needed either, the module is freshly allocated
    // in the youngest generation, and if a collection promotes it mid-loop the
    // promotion hook puts it in the remembered set.
    if (tp_module->aux.data != nullptr) {
        auto *slot = O_SLOT(module, tp_module);
        const auto funcs = (FunctionDef **) tp_module->aux.data;

        int f_count = 0;
        for (auto **cursor = funcs; *cursor != nullptr; cursor++) {
            auto func = FunctionNew(isolate, module, nullptr, *cursor);
            if (!func)
                return {};

            slot[f_count++] = (OObject *) func.get();
        }
    }

    return handle;
}

HOType orbiter::datatype::ModuleTypeInit(Isolate *isolate) {
    auto module = MakeType(isolate, "Module", InstanceType::MODULE, 0, 0, 0);
    return module;
}

HOType orbiter::datatype::ModuleTypeNew(Isolate *isolate, ORString *name, ORString *doc, const U16 exported,
                                        const U16 slots) {
    const auto total_props = exported + 3; // name + doc + modspec

    auto module = MakeTypeExtended(isolate, ORSTRING_TO_CSTR(name), InstanceType::MODULE, 0, total_props, slots);
    if (module) {
        if (!TIPropertyAdd(module.get(), "__name__", (OObject *) name, 0,
                           PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC))
            return {};

        if (!TIPropertyAdd(module.get(), "__doc__", (OObject *) doc, 0,
                           PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC))
            return {};

        // Real value will be added by the import system when loading the module
        if (!TIPropertyAdd(module.get(), "__spec__", nullptr, 0,
                           PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC))
            return {};
    }

    return module;
}

HOType orbiter::datatype::ModuleTypeNew(Isolate *isolate, const ModuleInit *init) {
    const auto name = ORStringNew(isolate, init->name);
    if (!name)
        return {};

    const auto doc = ORStringNew(isolate, init->doc);
    if (!doc)
        return {};

    int exp_count = 0;
    int f_count = 0;
    if (init->bulk != nullptr) {
        for (auto cursor = init->bulk; cursor->name != nullptr; cursor++) {
            exp_count += 1;

            if (cursor->is_func)
                f_count += 1;
        }
    }

    auto module = ModuleTypeNew(isolate, name.get(), doc.get(), exp_count, f_count);
    if (!module)
        return {};

    const FunctionDef **f_exported = nullptr;
    if (f_count > 0) {
        memory::IsolateAllocator allocator(isolate);

        f_exported = allocator.alloc<const FunctionDef *>(sizeof(FunctionDef *) * (f_count + 1));
        if (f_exported == nullptr)
            return {};

        f_exported[f_count] = nullptr;

        module->aux.data = f_exported;
        module->aux.dtor = ModuleAUXDtor;
    }

    f_count = 0;

    for (auto cursor = init->bulk; cursor != nullptr && cursor->name != nullptr; cursor++) {
        HOObject value;

        if (cursor->is_func) {
            f_exported[f_count] = cursor->prop.func;

            if (!TIPropertyAdd(module.get(), cursor->name, nullptr, f_count,
                               PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC | PropertyFlag::IN_OBJECT))
                return {};

            f_count += 1;
        } else {
            value = HOObject(cursor->prop.object);

            if (!TIPropertyAdd(module.get(), cursor->name, value.get(), 0,
                               PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC))
                return {};
        }
    }

    return module;
}

HOType orbiter::datatype::ModuleTypeNew(const Code *code, ORString *name) {
    auto *isolate = O_GET_ISOLATE(code);

    auto module = ModuleTypeNew(isolate, name, code->doc, code->exported.length, code->slots_count);
    if (module) {
        for (auto i = 0; i < code->exported.length; i++) {
            const auto *symbol = code->exported.symbols + i;

            PropertyFlag pd{};
            if (ENUMBITMASK_ISTRUE(symbol->flags, VariableFlags::CONSTANT))
                pd = PropertyFlag::IS_CONSTANT;

            if (ENUMBITMASK_ISTRUE(symbol->flags, VariableFlags::PUBLIC))
                pd = PropertyFlag::IS_PUBLIC;

            if (!TIPropertyAddInline(module.get(), (OObject *) symbol->name, symbol->slot, pd))
                return {};
        }
    }

    return module;
}
