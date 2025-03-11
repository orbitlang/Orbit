// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/code.h>

using namespace orbiter::datatype;

bool orbiter::datatype::CodeTypeSetup(TypeInfo *self) {
    return true;
}

HCode orbiter::datatype::CodeNew(Isolate *isolate, const unsigned char *m_code, List *unknown_symbols,
                                 List *static_resources, U32 m_size, U16 slots_count, U16 stack_size) {
    auto *code = MakeObject<Code>(isolate, InstanceType::CODE);

    if (code != nullptr) {
        code->codes = nullptr;
        code->unknown_symbols = O_FAST_INCREF(unknown_symbols);
        code->static_resources = O_FAST_INCREF(static_resources);
        code->doc = nullptr;

        code->exported.symbols = nullptr;
        code->exported.length = 0;

        code->m_code = m_code;
        code->m_end = m_code + m_size;

        code->slots_count = slots_count;
        code->stack_size = stack_size;
    }

    O_GC_TRACK_RETURN(isolate, code, false);
}

HOType orbiter::datatype::CodeTypeInit(Isolate *isolate) {
    auto code = MakeType(isolate, InstanceType::CODE, sizeof(Code) - sizeof(OObject), 0, 0);
    return code;
}
