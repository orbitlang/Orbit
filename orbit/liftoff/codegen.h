// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_CODEGEN_H_
#define ORBIT_LIFTOFF_CODEGEN_H_

#include <orbit/orbiter/memory/iallocator.h>

#include <orbit/liftoff/ir/ircontext.h>

#include <orbit/orbiter/datatype/code.h>

namespace liftoff {
    class Codegen {
        orbiter::memory::IsolateAllocator allocator_;

        ir::IRContext *ir_;

        static unsigned char *EmitOpcodes(const ir::BasicBlock *block, unsigned char *m_code);

        U32 CalculateCodeSize() const;

        void ExportNativeBindings(const orbiter::datatype::HCode &code);

        void ExportSymbols(orbiter::datatype::HCode &code);
    public:
        explicit Codegen(ir::IRContext *ir) noexcept : allocator_(ir->GetIsolate()), ir_(ir) {
        }

        orbiter::datatype::HCode Generate() noexcept;
    };
}

#endif // |ORBIT_LIFTOFF_CODEGEN_H_
