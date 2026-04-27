// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/datatype/module.h>

using namespace orbiter::datatype;

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

bool orbiter::datatype::ModuleSetup(TypeInfo *self) {
    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.to_string = ModuleToString;

    return true;
}

HModule orbiter::datatype::ModuleNew(TypeInfo *tp_module) {
    const auto *isolate = O_GET_ISOLATE(tp_module);

    assert(tp_module->i_type == InstanceType::MODULE);

    auto *module = MakeObject<Module>(tp_module);

    O_GC_TRACK_RETURN(isolate, module, true);
}

HOType orbiter::datatype::ModuleInit(Isolate *isolate) {
    auto module = MakeType(isolate, "Module", InstanceType::MODULE, 0, 0, 0);
    return module;
}

HOType orbiter::datatype::ModuleTypeNew(Isolate *isolate, ORString *name, ORString *doc, const U16 exported,
                                        const U16 slots) {
    const auto total_props = exported + 2; // name + doc

    auto module = MakeTypeExtended(isolate, ORSTRING_TO_CSTR(name), InstanceType::MODULE, 0, total_props, slots);
    if (module) {
        if (!TIPropertyAdd(module.get(), "__name__", (OObject *) name, 0,
                           PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC))
            return {};

        if (!TIPropertyAdd(module.get(), "__doc__", (OObject *) doc, 0,
                           PropertyFlag::IS_CONSTANT | PropertyFlag::IS_PUBLIC))
            return {};
    }

    return module;
}

HOType orbiter::datatype::ModuleTypeNew(const Code *code, ORString *name) {
    auto *isolate = O_GET_ISOLATE(code);

    auto module = ModuleTypeNew(isolate, name, nullptr, code->exported.length, code->slots_count);
    if (module) {
        for (auto i = 0; i < code->exported.length; i++) {
            const auto *symbol = code->exported.symbols + i;

            PropertyFlag pd{};
            if (ENUMBITMASK_ISTRUE(symbol->flags, VariableFlags::CONSTANT))
                pd = PropertyFlag::IS_CONSTANT;

            if (!TIPropertyAddInline(module.get(), (OObject *) symbol->name, symbol->slot, pd))
                return {};
        }
    }

    return module;
}
