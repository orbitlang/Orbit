// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/datatype/generator.h>

using namespace orbiter::datatype;

bool orbiter::datatype::GeneratorTypeSetup(TypeInfo *self) {
    return true;
}

HGenerator orbiter::datatype::GeneratorNew(Fiber *fiber, Function *base) {
    auto *gen = MakeObject<Generator>(fiber->isolate, InstanceType::GENERATOR);

    if (gen == nullptr)
        return {};

    auto *regs = &fiber->vm.regs;
    auto *stack = &fiber->vm.stack;

    // TODO: impl this!

    O_GC_TRACK_RETURN(fiber->isolate, gen, false);
}

HOType orbiter::datatype::GeneratorTypeInit(Isolate *isolate) {
    auto gen = MakeType(isolate, InstanceType::GENERATOR, sizeof(Generator) - sizeof(OObject), 0, 0);
    return gen;
}
