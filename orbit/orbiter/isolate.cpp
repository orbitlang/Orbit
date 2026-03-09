// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/atom.h>
#include <orbit/orbiter/datatype/ctbuilder.h>
#include <orbit/orbiter/datatype/closure.h>
#include <orbit/orbiter/datatype/code.h>
#include <orbit/orbiter/datatype/context.h>
#include <orbit/orbiter/datatype/decimal.h>
#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/future.h>
#include <orbit/orbiter/datatype/generator.h>
#include <orbit/orbiter/datatype/list.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/nativefunc.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/datatype/rawptr.h>
#include <orbit/orbiter/datatype/type.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/memory/gc.h>

#include <orbit/orbiter/native/loader.h>

#include <orbit/orbiter/isolate.h>
#include <orbit/orbiter/defer.h>
#include <orbit/orbiter/fpool.h>

using namespace orbiter;
using namespace orbiter::datatype;

Isolate::~Isolate() {
    delete this->dpool_;
    delete this->fpool_;
    delete this->gc;
    delete this->loader_;

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
    if (!isolate->allocator_->Initialize()) {
        delete isolate;

        return nullptr;
    }

    auto allocator = memory::IsolateAllocator(isolate);
    isolate->panic_cache = allocator.alloc<Panic>(sizeof(Panic));
    if (isolate->panic_cache == nullptr)
        goto ERROR;

    isolate->panic.Reset();

    isolate->dpool_ = new DeferPool(isolate);
    isolate->fpool_ = new FiberPool(isolate, -1, -1, -1);

    isolate->gc = new memory::GC(isolate);

    INIT_TYPE(InstanceType::TYPE, TypeInit);

    INIT_TYPE(InstanceType::ATOM, AtomTypeInit);
    INIT_TYPE(InstanceType::CLASS, ClassTypeInit);
    INIT_TYPE(InstanceType::CLOSURE, ClosureTypeInit);
    INIT_TYPE(InstanceType::CODE, CodeTypeInit);
    INIT_TYPE(InstanceType::CONTEXT, ContextInit);
    INIT_TYPE(InstanceType::DECIMAL, DecimalTypeInit);
    INIT_TYPE(InstanceType::DICT, DictTypeInit);
    INIT_TYPE(InstanceType::ERROR, ErrorTypeInit);
    INIT_TYPE(InstanceType::FUNCTION, FunctionTypeInit);
    INIT_TYPE(InstanceType::FUTURE, FutureTypeInit);
    INIT_TYPE(InstanceType::GENERATOR, GeneratorTypeInit);
    INIT_TYPE(InstanceType::LIST, ListTypeInit);
    INIT_TYPE(InstanceType::MODULE, ModuleInit);
    INIT_TYPE(InstanceType::NATIVE_FUNC, NativeFuncTypeInit);
    INIT_TYPE(InstanceType::NUMBER, NumberTypeInit);
    INIT_TYPE(InstanceType::RAWPTR, RawPtrTypeInit);
    INIT_TYPE(InstanceType::STRING, ORStringTypeInit);
    INIT_TYPE(InstanceType::TRAIT, TraitTypeInit);
    INIT_TYPE(InstanceType::TUPLE, TupleTypeInit);

    // *****************************************************************************************************************

    SETUP_TYPE(InstanceType::TYPE, TypeSetup);

    SETUP_TYPE(InstanceType::ATOM, AtomTypeSetup);
    SETUP_TYPE(InstanceType::CLASS, ClassTypeSetup);
    SETUP_TYPE(InstanceType::CLOSURE, ClosureTypeSetup);
    SETUP_TYPE(InstanceType::CODE, CodeTypeSetup);
    SETUP_TYPE(InstanceType::CONTEXT, ContextSetup);
    SETUP_TYPE(InstanceType::DECIMAL, DecimalTypeSetup);
    SETUP_TYPE(InstanceType::DICT, DictTypeSetup);
    SETUP_TYPE(InstanceType::ERROR, ErrorTypeSetup);
    SETUP_TYPE(InstanceType::FUNCTION, FunctionTypeSetup);
    SETUP_TYPE(InstanceType::FUTURE, FutureTypeSetup);
    SETUP_TYPE(InstanceType::GENERATOR, GeneratorTypeSetup);
    SETUP_TYPE(InstanceType::LIST, ListTypeSetup);
    SETUP_TYPE(InstanceType::MODULE, ModuleSetup);
    SETUP_TYPE(InstanceType::NATIVE_FUNC, NativeFuncTypeSetup);
    SETUP_TYPE(InstanceType::NUMBER, NumberTypeSetup);
    SETUP_TYPE(InstanceType::RAWPTR, RawPtrTypeSetup);
    SETUP_TYPE(InstanceType::STRING, ORStringTypeSetup);
    SETUP_TYPE(InstanceType::TRAIT, TraitTypeSetup);
    SETUP_TYPE(InstanceType::TUPLE, TupleTypeSetup);

    // Build Error instance for OOMError
    isolate->oom_error_ = (OObject *) ErrorNew(isolate,
                                               MemoryError::Details[MemoryError::Reason::ID],
                                               nullptr,
                                               MemoryError::Details[MemoryError::Reason::HEAP]).release();
    if (isolate->oom_error_ == nullptr)
        goto ERROR;

    isolate->loader_ = new native::Loader(isolate);
    if (!isolate->loader_->Initialize())
        goto ERROR;

    return isolate;

ERROR:
    delete isolate;

    return nullptr;
#undef INIT_TYPE
#undef SETUP_TYPE
}
