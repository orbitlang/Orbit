// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/orbiter/datatype/generator.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// TYPE OPS — COMPARISON
// *********************************************************************************************************************

/// Generators are stateful execution contexts — identity equality only.
static bool GeneratorEqual(const OObject *left, const OObject *right) {
    return left == right;
}

// *********************************************************************************************************************
// TYPE OPS — ITERATION
// *********************************************************************************************************************

/// A generator is its own iterator.
static OObject *GeneratorGetIter(OObject *self) {
    return self;
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// A generator is truthy as long as it has not been exhausted.
static bool GeneratorToBool(const OObject *self) {
    return ((const Generator *) self)->state.load(std::memory_order_relaxed) != GeneratorState::EXHAUSTED;
}

/// Format: "generator <name> ready/running/exhausted at 0xADDR"
static OObject *GeneratorToString(orbiter::Isolate *isolate, const OObject *self) {
    const auto *gen = (const Generator *) self;

    const char *state_str;

    switch (gen->state.load(std::memory_order_relaxed)) {
        case GeneratorState::READY:     state_str = "ready";     break;
        case GeneratorState::RUNNING:   state_str = "running";   break;
        case GeneratorState::EXHAUSTED: state_str = "exhausted"; break;
        default:                        state_str = "unknown";   break;
    }

    const auto *fn_name = ORSTRING_TO_CSTR(gen->base->shared->name);

    const auto s = ORStringFormat(isolate, "generator <%s> %s at %p", fn_name, state_str, self);

    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool GeneratorDtor(const Generator *self) {
    const orbiter::memory::IsolateAllocator allocator(O_GET_ISOLATE(self));

    allocator.free(self->regs_dump);

    return true;
}

void GeneratorTrace(const Generator *self, const GCTraceCallback callback, const MSize epoch) {
    callback((OObject *) self->base, epoch);

    // Trace VM registry dump
    for (auto cursor = self->regs_dump; cursor < self->params; cursor++) {
        if (O_IS_OBJECT(*cursor))
            callback(*cursor, epoch);
    }

    // Trace function parameters (if any)
    for (auto cursor = self->params; cursor < self->stack; cursor++) {
        if (O_IS_OBJECT(*cursor))
            callback(*cursor, epoch);
    }

    // Trace function stack
    for (auto i = 0; i < self->stack_size; i++) {
        auto **cursor = self->stack + i;
        if (O_IS_OBJECT(*cursor))
            callback(*cursor, epoch);
    }
}

bool orbiter::datatype::GeneratorTypeSetup(TypeInfo *self) {
    self->dtor  = (DtorFn)  GeneratorDtor;
    self->trace = (TraceFn) GeneratorTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal    = GeneratorEqual;
    ops.get_iter = GeneratorGetIter;
    ops.to_bool  = GeneratorToBool;
    ops.to_string = GeneratorToString;
    ops.to_repr   = GeneratorToString;

    return true;
}

HGenerator orbiter::datatype::GeneratorNew(const Fiber *fiber, Function *base, const U16 param_size) {
    auto *gen = MakeObject<Generator>(fiber->isolate, InstanceType::GENERATOR);
    if (gen == nullptr)
        return {};

    assert(base != nullptr);

    const auto *regs = &fiber->vm.regs;
    const auto *stack = &fiber->vm.stack;

    memory::IsolateAllocator allocator(fiber->isolate);

    auto *buffer = allocator.alloc<OObject *>(param_size
                                              + (base->shared->code->stack_size + kGeneralPurposeRegistersCount)
                                              * sizeof(void *));
    if (buffer == nullptr)
        return {};

    gen->base = base;
    gen->regs_dump = buffer;
    gen->params = buffer + kGeneralPurposeRegistersCount;
    gen->stack = gen->params + param_size;
    gen->stack_size = 0;
    gen->acquired = 0;
    gen->state = GeneratorState::READY;

    memory::MemoryCopy(gen->regs_dump, regs, (kGeneralPurposeRegistersCount * sizeof(void *)));
    memory::MemoryCopy(gen->params, (stack->stack + regs->SP.reg) - param_size, param_size);

    gen->IP = (PtrSize) base->shared->code->m_code;

    O_GC_TRACK_RETURN(fiber->isolate, gen, true);
}

HOType orbiter::datatype::GeneratorTypeInit(Isolate *isolate) {
    auto gen = MakeType(isolate, "Generator", InstanceType::GENERATOR, sizeof(Generator) - sizeof(OObject), 0, 0);
    return gen;
}
