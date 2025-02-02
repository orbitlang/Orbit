// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/module.h>

using namespace orbiter::datatype;

TypeInfo *orbiter::datatype::ModuleInit(Isolate *isolate) {
    auto *module = MakeType(isolate, InstanceType::MODULE, 0, 0, 0);
    return module;
}

TypeInfo *orbiter::datatype::ModuleNew(Isolate *isolate, ORString *name, ORString *doc, U16 exported, U16 slots) {
    const auto total_props = exported + 2; // name + doc

    auto *module = MakeTypeExtended(isolate, InstanceType::MODULE, 0, total_props, slots);
    if (module != nullptr) {
        if (!TIPropertyAdd(module, "__name__", (OObject *) name, PropertyFlag::IS_CONSTANT))
            goto ERROR;

        if (!TIPropertyAdd(module, "__doc__", (OObject *) doc, PropertyFlag::IS_CONSTANT))
            goto ERROR;
    }

    return module;

ERROR:

    Release(module);

    return nullptr;
}

TypeInfo *orbiter::datatype::ModuleNew(Code *code, ORString *name) {
    auto *isolate = O_GET_ISOLATE(code);

    auto *module = ModuleNew(isolate, name, nullptr, code->exported.length, code->slots_count);
    if (module != nullptr) {
        for (auto i = 0; i < code->exported.length; i++) {
            const auto *symbol = code->exported.symbols + i;

            PropertyFlag pd{};
            if (ENUMBITMASK_ISTRUE(symbol->flags, VariableFlags::CONSTANT))
                pd = PropertyFlag::IS_CONSTANT;

            if (!TIPropertyAdd(module, (OObject *) symbol->name, symbol->slot, pd)) {
                Release(module);
                return nullptr;
            }
        }
    }

    return module;
}

bool orbiter::datatype::ModuleSetup(TypeInfo *self) {
    return true;
}
