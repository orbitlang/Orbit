// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_IRCONTEXT_H_
#define ORBIT_LIFTOFF_IR_IRCONTEXT_H_

#include <orbit/datatype.h>

#include <orbit/liftoff/ir/basicblock.h>

namespace liftoff::ir {
    enum class IRContextType {
        FUNCTION,
        METHOD,
        MODULE
    };

    class IRContext {
        BasicBlock *entry_ = nullptr;

        U32 logical_counter_ = 0;

        friend class Builder;

    public:
        IRContextType type_;

        explicit IRContext(const IRContextType type) noexcept: type_(type) {
        }

        I32 GetIncRVirtCounter() noexcept {
            return (I32) this->logical_counter_++;
        }
    };
}

#endif // !ORBIT_LIFTOFF_IR_IRCONTEXT_H_
