// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/builder.h>

#include <orbit/liftoff/ir/jblock.h>

using namespace liftoff::ir;
using namespace orbiter::datatype;

JBlock::JBlock(Builder *builder, JBlockType type, ORString *label) : builder_(builder) {
    auto *jc_prev = builder_->context->j_chain;

    this->prev = jc_prev;

    if (jc_prev != nullptr && jc_prev->type == JBlockType::LABEL) {
        this->begin = jc_prev->begin;
        this->end = jc_prev->end;

        this->type = type;

        builder->context->j_chain = this;

        return;
    }

    this->begin = builder->CreateBasicBlock();
    this->end = builder->CreateBasicBlock();

    this->type = type;

    this->label = label; // NO INCREF here!

    builder->context->j_chain = this;
}

JBlock::JBlock(Builder *builder, Instruction *value) : JBlock(builder, JBlockType::SYNC, nullptr) {
    this->value = value;
}

JBlock::JBlock(Builder *builder, const JBlockType type) : builder_(builder), type(type) {
    this->prev = builder_->context->j_chain;
    this->builder_->context->j_chain = this;
}

JBlock::~JBlock() {
    this->builder_->context->j_chain = this->prev;
}

JBlock *JBlock::FindLabeledBlock(const ORString *label) {
    auto *cursor = this;

    while (cursor != nullptr) {
        if (label == nullptr) {
            if (cursor->type != JBlockType::SYNC)
                return cursor;
        } else if (cursor->label != nullptr && ORStringEqual(label, cursor->label))
            return cursor;

        cursor = cursor->prev;
    }

    return nullptr;
}
