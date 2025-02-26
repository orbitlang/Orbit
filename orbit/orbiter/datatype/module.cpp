// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <orbit/orbiter/datatype/module.h>

using namespace orbiter::datatype;

HModule orbiter::datatype::ModuleNew(TypeInfo *tp_module) {
    auto *isolate = O_GET_ISOLATE(tp_module);

    assert(tp_module->i_type == InstanceType::MODULE);

    auto *module = MakeObject<Module>(tp_module);

    O_GC_TRACK_RETURN(isolate, module, true);
}

HOType orbiter::datatype::ModuleInit(Isolate *isolate) {
    auto module = MakeType(isolate, InstanceType::MODULE, 0, 0, 0);
    return module;
}

HOType orbiter::datatype::ModuleTypeNew(Isolate *isolate, ORString *name, ORString *doc, U16 exported, U16 slots) {
    const auto total_props = exported + 2; // name + doc

    auto module = MakeTypeExtended(isolate, InstanceType::MODULE, 0, total_props, slots);
    if (!module) {
        if (!TIPropertyAdd(module.get(), "__name__", (OObject *) name, PropertyFlag::IS_CONSTANT))
            return {};

        if (!TIPropertyAdd(module.get(), "__doc__", (OObject *) doc, PropertyFlag::IS_CONSTANT))
            return {};
    }

    return module;
}

HOType orbiter::datatype::ModuleTypeNew(Code *code, ORString *name) {
    auto *isolate = O_GET_ISOLATE(code);

    auto module = ModuleTypeNew(isolate, name, nullptr, code->exported.length, code->slots_count);
    if (!module) {
        for (auto i = 0; i < code->exported.length; i++) {
            const auto *symbol = code->exported.symbols + i;

            PropertyFlag pd{};
            if (ENUMBITMASK_ISTRUE(symbol->flags, VariableFlags::CONSTANT))
                pd = PropertyFlag::IS_CONSTANT;

            if (!TIPropertyAdd(module.get(), (OObject *) symbol->name, symbol->slot, pd))
                return {};
        }
    }

    return module;
}

bool orbiter::datatype::ModuleSetup(TypeInfo *self) {
    return true;
}
