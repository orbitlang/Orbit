// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/atom.h>
#include <orbit/orbiter/datatype/code.h>
#include <orbit/orbiter/datatype/context.h>
#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/type.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/memory/gc.h>

#include <orbit/orbiter/isolate.h>
#include <orbit/orbiter/fpool.h>

using namespace orbiter;
using namespace orbiter::datatype;

Isolate::~Isolate() {
    delete this->fpool_;
    delete this->gc;

    this->allocator_->Finalize();
    delete this->allocator_;
}

Isolate *Isolate::New() {
#define INIT_TYPE(num, fn)                                                      \
    do {                                                                        \
        if((isolate->primitive[(int) num] = fn(isolate).get_inc()) == nullptr)  \
            goto ERROR;                                                         \
    } while(0)

#define SETUP_TYPE(num, fn)                             \
    do {                                                \
        if(!fn(isolate->primitive[(int) num]))          \
            goto ERROR;                                 \
    } while(0)

    auto *isolate = static_cast<Isolate *>(operator new(sizeof(Isolate)));
    if (isolate == nullptr)
        return nullptr;

    isolate->allocator_ = new stratum::Memory();
    if (!isolate->allocator_->Initialize())
        goto ERROR;

    isolate->fpool_ = new FiberPool(isolate, -1, -1, -1);

    isolate->gc = new memory::GC(isolate);

    INIT_TYPE(InstanceType::TYPE, TypeInit);

    INIT_TYPE(InstanceType::ATOM, AtomTypeInit);
    INIT_TYPE(InstanceType::CODE, CodeTypeInit);
    INIT_TYPE(InstanceType::CONTEXT, ContextInit);
    INIT_TYPE(InstanceType::DECIMAL, DecimalTypeInit);
    INIT_TYPE(InstanceType::FUNCTION, FunctionTypeInit);
    INIT_TYPE(InstanceType::LIST, ListTypeInit);
    INIT_TYPE(InstanceType::MODULE, ModuleInit);
    INIT_TYPE(InstanceType::NUMBER, NumberTypeInit);
    INIT_TYPE(InstanceType::STRING, ORStringTypeInit);
    INIT_TYPE(InstanceType::TUPLE, TupleTypeInit);

    // *****************************************************************************************************************

    SETUP_TYPE(InstanceType::TYPE, TypeSetup);

    SETUP_TYPE(InstanceType::ATOM, AtomTypeSetup);
    SETUP_TYPE(InstanceType::CODE, CodeTypeSetup);
    SETUP_TYPE(InstanceType::CONTEXT, ContextSetup);
    SETUP_TYPE(InstanceType::DECIMAL, DecimalTypeSetup);
    SETUP_TYPE(InstanceType::FUNCTION, FunctionTypeSetup);
    SETUP_TYPE(InstanceType::LIST, ListTypeSetup);
    SETUP_TYPE(InstanceType::MODULE, ModuleSetup);
    SETUP_TYPE(InstanceType::NUMBER, NumberTypeSetup);
    SETUP_TYPE(InstanceType::STRING, ORStringTypeSetup);
    SETUP_TYPE(InstanceType::TUPLE, TupleTypeSetup);

    return isolate;

ERROR:
    delete isolate;

    return nullptr;
#undef INIT_TYPE
#undef SETUP_TYPE
}
