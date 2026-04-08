// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/vm.h>

#include <orbit/liftoff/scanner/scanner.h>
#include <orbit/liftoff/parser/parser.h>

#include <orbit/liftoff/ir/irbuilder.h>
#include <orbit/liftoff/ir/linearscan.h>

#include <orbit/liftoff/codegen.h>
#include <orbit/liftoff/optimizer.h>

#include <orbit/liftoff/compiler.h>

using namespace liftoff;
using namespace liftoff::ir;
using namespace liftoff::parser;

orbiter::datatype::HList Compiler::BuildCodesList(const IRContext *ir) {
    orbiter::datatype::HList codes{};

    const auto count = ir->GetSubcontextCount();
    if (count > 0) {
        codes = orbiter::datatype::ListNew(this->isolate_, count);
        if (!codes)
            return {};

        for (int i = 0; i < count; i++) {
            if (!ListAppend(codes.get(), (orbiter::datatype::OObject *) this->Compile(ir->GetSubContext(i)).get()))
                return {};
        }
    }

    return codes;
}

orbiter::datatype::HCode Compiler::Compile(IRContext *ir) {
    /*
     * IR Generation
     *   -> Step 1: Optimization
     *   -> Step 2: Instruction numbering
     *   -> Step 3: Liveness analysis
     *   -> Step 4: Register allocation
     *   -> Step 5: Code generation
     */

    // Step 1: Optimization (DCE, constant folding, ...)
    Optimizer optimizer(ir, this->level_);
    optimizer.Optimize();

    // Step 2: Assign final stable instruction offsets on the fully transformed IR
    ir->SlotIndexes();

    // Step 3-4: Compute live intervals and allocate registers
    auto intervals = ir->ComputeLiveIntervals();
    LinearScan(ir, orbiter::kGeneralPurposeRegistersCount).Allocate(intervals);

    // Step 5: Generate machine code
    auto code = Codegen(ir).Generate();
    if (code) {
        if (ir->GetSubcontextCount() > 0) {
            code->codes = this->BuildCodesList(ir).get_inc();
            if (code->codes == nullptr)
                return {};
        }
    }

    return code;
}

orbiter::datatype::HCode Compiler::Compile(const char *filename, scanner::Scanner &scanner) {
    Parser parser(this->isolate_, filename, scanner);

    auto ast = parser.Parse();
    if (!ast) {
        auto error = parser.GetLastError();
        printf("%s\n", error.message);
        assert(false);
    }

    IRBuilder builder(this->isolate_, this->is_module_);

    const auto ir = builder.Generate(ast);
    if (!ir)
        assert(false);

    return this->Compile(ir.get());
}

orbiter::datatype::HCode Compiler::Compile(const char *filename, FILE *fd) {
    scanner::Scanner scanner(this->isolate_, fd, nullptr, nullptr);

    return this->Compile(filename, scanner);
}
