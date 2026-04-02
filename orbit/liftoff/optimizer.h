// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_OPTIMIZER_H_
#define ORBIT_LIFTOFF_OPTIMIZER_H_

#include <orbit/liftoff/olevel.h>
#include <orbit/liftoff/ir/ircontext.h>

namespace liftoff {
    class Optimizer {
        ir::IRContext *ir_;

        OptimizationLevel level_;

        static bool HasSideEffects(orbiter::OPCode opcode);

        void DeadCodeElimination() const;

    public:
        explicit Optimizer(ir::IRContext *ir, const OptimizationLevel level) : ir_(ir), level_(level) {
        }

        void Optimize();
    };
}

#endif // !ORBIT_LIFTOFF_OPTIMIZER_H_
