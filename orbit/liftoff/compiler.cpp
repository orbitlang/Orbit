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
     *   -> Step 2: Liveness analysis          (renumbers instructions, then computes intervals)
     *   -> Step 3: Caller-save spill insertion (save/reload values live across calls)
     *   -> Step 4: Liveness analysis          (recomputed on the rewritten IR)
     *   -> Step 5: Register allocation
     *   -> Step 6: Code generation
     */

    // Step 1: Optimization (DCE, constant folding, ...)
    Optimizer optimizer(ir, this->level_);
    optimizer.Optimize();

    // Step 2: Liveness analysis. ComputeLiveIntervals renumbers the instructions
    ir->ComputeLiveIntervals();

    // Step 3: Caller-save spill insertion. Every value live across a call is
    // materialized to the stack and reloaded at its later uses as real IR. This
    // both preserves values the callee would clobber and turns each reload into a
    // fresh, non-pinned SSA value.
    ir->CallerSaveSpiller();

    // Step 4: Liveness analysis, recomputed. The spill pass inserted instructions
    // and shifted the layout, so the intervals fed to allocation must be rebuilt
    auto intervals = ir->ComputeLiveIntervals();

    // Step 5: Register allocation over the finalized IR.
    LinearScan(ir, orbiter::kAllocatableRegistersCount).Allocate(intervals);

    // Step 6: Generate machine code
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
