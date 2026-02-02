// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/generator.h>

using namespace orbiter::datatype;

bool orbiter::datatype::GeneratorTypeSetup(TypeInfo *self) {
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

    gen->base = O_FAST_INCREF(base);
    gen->regs_dump = buffer;
    gen->params = buffer + kGeneralPurposeRegistersCount;
    gen->stack = gen->params + param_size;
    gen->stack_size = 0;
    gen->acquired = 0;
    gen->state = GeneratorState::READY;

    memory::MemoryCopy(gen->regs_dump, regs, (kGeneralPurposeRegistersCount * sizeof(void *)));
    memory::MemoryCopy(gen->params, (stack->stack + regs->SP.reg) - param_size, param_size);

    // This can be optimized: stack parameters already have their incref. Just copy them here and remove them from the stack.
    // No other action is required; you only need to incref the register duplicates!
    for (auto *cursor = gen->regs_dump; cursor < gen->params; cursor++)
        O_INCREF(*cursor);

    gen->IP = (PtrSize) base->shared->code->m_code;

    O_GC_TRACK_RETURN(fiber->isolate, gen, false);
}

HOType orbiter::datatype::GeneratorTypeInit(Isolate *isolate) {
    auto gen = MakeType(isolate, InstanceType::GENERATOR, sizeof(Generator) - sizeof(OObject), 0, 0);
    return gen;
}
