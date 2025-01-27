// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/code.h>

using namespace orbiter::datatype;

bool orbiter::datatype::CodeTypeSetup(TypeInfo *self) {
    return true;
}

HCode orbiter::datatype::CodeNew(Isolate *isolate, List *codes, List *names, List *static_resources, ORString *doc,
                                 const unsigned char *m_code, U32 m_size, U16 known_length, U16 stack_size) {
    auto *code = MakeObject<Code>(isolate, InstanceType::CODE);

    if (code != nullptr) {
        code->codes = O_VFY_INCREF(codes);
        code->names = O_VFY_INCREF(names);
        code->static_resources = O_VFY_INCREF(static_resources);
        code->doc = O_VFY_INCREF(doc);

        code->m_code = m_code;
        code->m_end = m_code + m_size;

        code->knames_length = known_length;
        code->stack_size = stack_size;
    }

    return HCode(code);
}

TypeInfo *orbiter::datatype::CodeTypeInit(Isolate *isolate) {
    auto *code = MakeType(isolate, InstanceType::CODE, sizeof(Code) - sizeof(OObject), 0, 0);
    return code;
}
