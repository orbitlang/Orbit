// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/vm.h>

#include <orbit/liftoff/scanner/scanner.h>
#include <orbit/liftoff/parser/parser.h>

#include <orbit/liftoff/ir/irbuilder.h>
#include <orbit/liftoff/ir/linearscan.h>

#include <orbit/liftoff/codegen.h>

#include <orbit/liftoff/compiler.h>

using namespace liftoff;
using namespace liftoff::ir;
using namespace liftoff::parser;

orbiter::datatype::HList Compiler::BuildCodesList(IRContext *ir) {
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
     *   -> Optimizations
     *   -> Liveness Analysis
     *   -> Linear Scan
     *   -> Phi Resolution
     *   -> Code Generation
     */

    // Step 1: Optimization
    // TODO: Implement optimization phase here...

    // Step 2: Perform Liveness Analysis
    ir->ComputeLiveIntervals();

    // Step 3-4: Allocate Registers / Phi resolution
    LinearScan(this->isolate_, orbiter::kGeneralPurposeRegistersCount).Allocate(ir);

    // Step 5: Generate machine code
    auto code = Codegen(this->isolate_).Generate(ir);
    if (code) {
        if (ir->GetSubcontextCount() > 0) {
            code->codes = this->BuildCodesList(ir).get();
            if (code->codes == nullptr)
                return {};
        }
    }

    return code;
}

orbiter::datatype::HCode Compiler::Compile(const char *filename, scanner::Scanner &scanner) {
    auto ast = Parser(this->isolate_, filename, scanner).Parse();
    if (!ast) {
        assert(false);
    }

    auto ir = IRBuilder(this->isolate_, this->level_).Generate(ast);
    if (ir == nullptr)
        assert(false);

    return this->Compile(ir);
}

orbiter::datatype::HCode Compiler::Compile(const char *filename, FILE *fd) {
    scanner::Scanner scanner(this->isolate_, fd, nullptr, nullptr);

    return this->Compile(filename, scanner);
}
