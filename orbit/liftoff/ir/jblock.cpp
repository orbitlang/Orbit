// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/builder.h>

#include <orbit/liftoff/ir/jblock.h>

using namespace liftoff::ir;
using namespace orbiter::datatype;

JBlock::JBlock(Builder *builder, JBlockType type, ORString *label) : builder_(builder) {
    auto *prev = builder_->context->j_chain;

    this->prev = prev;

    if (prev != nullptr && prev->type == JBlockType::LABEL) {
        this->begin = prev->begin;
        this->end = prev->end;

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

JBlock::~JBlock() {
    this->builder_->context->j_chain = this->prev;
}

JBlock *JBlock::FindLabeledBlock(const ORString *label) {
    auto *cursor = this;

    if (label == nullptr)
        return this;

    while (cursor != nullptr) {
        if (cursor->label != label && ORStringEqual(label, cursor->label))
            return cursor;

        cursor = this->prev;
    }

    return nullptr;
}
