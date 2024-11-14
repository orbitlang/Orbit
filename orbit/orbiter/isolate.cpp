// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/atom.h>
#include <orbit/orbiter/datatype/code.h>
#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/type.h>

#include <orbit/orbiter/isolate.h>

using namespace orbiter;
using namespace orbiter::datatype;

Isolate *Isolate::New() {
#define INIT_TYPE(num, fn)                                              \
    do {                                                                \
        if((isolate->primitive[(int) num] = fn(isolate)) == nullptr)    \
            goto ERROR;                                                 \
    } while(0)

#define SETUP_TYPE(num, fn)                             \
    do {                                                \
        if(!fn(isolate, isolate->primitive[(int) num])) \
            goto ERROR;                                 \
    } while(0)

    auto *isolate = static_cast<Isolate *>(operator new(sizeof(Isolate)));
    if (isolate == nullptr)
        return nullptr;

    new(&isolate->allocator_) stratum::Memory();

    if(!isolate->allocator_.Initialize()) {
        delete isolate;

        return nullptr;
    }

    INIT_TYPE(InstanceType::TYPE, TypeInit);

    INIT_TYPE(InstanceType::ATOM, AtomTypeInit);
    INIT_TYPE(InstanceType::CODE, CodeTypeInit);
    INIT_TYPE(InstanceType::DECIMAL, DecimalTypeInit);
    INIT_TYPE(InstanceType::FUNCTION, FunctionTypeInit);
    INIT_TYPE(InstanceType::NUMBER, NumberTypeInit);
    INIT_TYPE(InstanceType::STRING, ORStringTypeInit);

    // *****************************************************************************************************************

    SETUP_TYPE(InstanceType::TYPE, TypeSetup);

    SETUP_TYPE(InstanceType::ATOM, AtomTypeSetup);
    SETUP_TYPE(InstanceType::CODE, CodeTypeSetup);
    SETUP_TYPE(InstanceType::DECIMAL, DecimalTypeSetup);
    SETUP_TYPE(InstanceType::FUNCTION, FunctionTypeSetup);
    SETUP_TYPE(InstanceType::NUMBER, NumberTypeSetup);
    SETUP_TYPE(InstanceType::STRING, ORStringTypeSetup);

    return isolate;

ERROR:
    isolate->allocator_.Finalize();

    delete isolate;

    return nullptr;
#undef INIT_TYPE
#undef SETUP_TYPE
}
