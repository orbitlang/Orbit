// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/liftoff/ir/builder.h>

#include <orbit/liftoff/ir/jblock.h>

using namespace liftoff::ir;
using namespace orbiter::datatype;

// *** JBlock (base) ***

JBlock::JBlock(Builder *builder, const JBlockType type) : builder_(builder), type(type) {
    this->prev = builder_->context->j_chain;

    builder_->context->j_chain = this;
}

JBlock::~JBlock() {
    this->builder_->context->j_chain = this->prev;
}

// *** JBlockTarget (LOOP, FOR_IN, LABEL) ***

JBlockTarget::JBlockTarget(Builder *builder, const JBlockType type, ORString *label) : JBlock(builder, type) {
    // LOOP/FOR_IN: if the previous block is a LABEL, inherit its begin/end targets
    if (type != JBlockType::LABEL && this->prev != nullptr && this->prev->type == JBlockType::LABEL) {
        const auto *lbl = (JBlockTarget *) this->prev;

        this->begin = lbl->begin;
        this->end = lbl->end;

        return;
    }

    this->begin = this->GetBuilder()->CreateBasicBlock();
    this->end = this->GetBuilder()->CreateBasicBlock();

    this->label = label;
}

// *** JBlockBranch (NIL_SAFE, TCF) ***

JBlockBranch::JBlockBranch(Builder *builder, const JBlockType type, BasicBlock *alt, BasicBlock *end)
    : JBlock(builder, type), alt(alt), end(end) {
}

// *** JBlockSync ***

JBlockSync::JBlockSync(Builder *builder, Instruction *value)
    : JBlock(builder, JBlockType::SYNC), value(value) {
}

// *** JBlockSwitch ***

JBlockSwitch::JBlockSwitch(Builder *builder, BasicBlock *end)
    : JBlock(builder, JBlockType::SWITCH), end(end) {
}

// *** Free functions ***

BasicBlock *liftoff::ir::GetJBlockBegin(const JBlock *block) {
    switch (block->type) {
        case JBlockType::LOOP:
        case JBlockType::FOR_IN:
        case JBlockType::LABEL:
            return ((JBlockTarget *) block)->begin;
        default:
            assert(false);
            return nullptr;
    }
}

BasicBlock *liftoff::ir::GetJBlockEnd(const JBlock *block) {
    switch (block->type) {
        case JBlockType::LOOP:
        case JBlockType::FOR_IN:
        case JBlockType::LABEL:
            return ((JBlockTarget *) block)->end;
        case JBlockType::NIL_SAFE:
        case JBlockType::TCF:
            return ((JBlockBranch *) block)->end;
        case JBlockType::SWITCH:
            return ((JBlockSwitch *) block)->end;
        default:
            assert(false);
            return nullptr;
    }
}

JBlock *liftoff::ir::FindLabeledBlock(JBlock *chain, const ORString *label) {
    auto *cursor = chain;

    while (cursor != nullptr) {
        if (label == nullptr) {
            if (cursor->type == JBlockType::LOOP
                || cursor->type == JBlockType::FOR_IN)
                return cursor;
        } else {
            const ORString *block_label = nullptr;

            if (cursor->type == JBlockType::LOOP
                || cursor->type == JBlockType::FOR_IN
                || cursor->type == JBlockType::LABEL)
                block_label = ((JBlockTarget *) cursor)->label;

            if (block_label != nullptr && ORStringEqual(label, block_label))
                return cursor;
        }

        cursor = cursor->prev;
    }

    return nullptr;
}
