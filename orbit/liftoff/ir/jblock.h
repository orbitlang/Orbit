// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_LIFTOFF_IR_JBLOCK_H_
#define ORBIT_LIFTOFF_IR_JBLOCK_H_

#include <orbit/orbiter/datatype/orstring.h>

#include <orbit/liftoff/ir/basicblock.h>

namespace liftoff::ir {
    class Builder;

    enum class JBlockType {
        FOR_IN,
        LABEL,
        LOOP,
        NIL_SAFE,
        TCF,
        SYNC,
        SWITCH
    };

    class JBlock {
        Builder *builder_;

    public:
        JBlock *prev = nullptr;

        union {
            BasicBlock *begin = nullptr;
            BasicBlock *alt;
        };

        BasicBlock *end = nullptr;

        Instruction *value = nullptr;

        orbiter::datatype::ORString *label = nullptr;

        JBlockType type = JBlockType::LOOP;

        JBlock(Builder *builder, JBlockType type, orbiter::datatype::ORString *label);

        JBlock(Builder *builder, JBlockType type);

        ~JBlock();

        JBlock *FindLabeledBlock(const orbiter::datatype::ORString *label);
    };
};

#endif // !ORBIT_LIFTOFF_IR_JBLOCK_H_
