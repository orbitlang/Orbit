// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <orbit/liftoff/ir/builder.h>

using namespace liftoff::ir;
using namespace orbiter;

Instruction *Builder::LoadStoreOffset(const OPCode opcode, const U16 offset) {
    auto *instr = this->CreateObject<LoadStoreWithOffsetInstr>(opcode, offset);

    this->AddInstruction(instr);

    instr->dest.virtID = this->context->GetIncRVirtCounter();

    return instr;
}

Instruction *Builder::CreateBinaryOp(const OPCode opcode, Object *left, Object *right) {
    auto *instr = this->CreateObject<BinaryOpInstr>(opcode);

    instr->dest.virtID = this->context->GetIncRVirtCounter();

    instr->left = left;
    instr->right = right;

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::CreateBinaryOpFlags(const OPCode opcode, const U8 flags, Object *left, Object *right) {
    auto *binOp = this->CreateObject<BinaryOpFlagsInstr>(opcode, flags);

    binOp->dest.virtID = this->context->GetIncRVirtCounter();

    binOp->left = left;
    binOp->right = right;

    this->AddInstruction(binOp);

    return binOp;
}

Instruction *Builder::CreateBranch(const OPCode opcode, BasicBlock *continuation, BasicBlock *destination) {
    auto *branch = this->CreateObject<BranchInstruction>(opcode, destination);

    this->AddInstruction(branch);

    if (continuation == nullptr)
        this->CreateAppendBasicBlock();
    else
        this->AppendBasicBlock(continuation);

    return branch;
}

Instruction *Builder::CreateUnaryOp(const OPCode opcode, Object *s_reg) {
    auto *unaryOp = this->CreateObject<UnaryOpInstr>(opcode);

    unaryOp->dest.virtID = this->context->GetIncRVirtCounter();

    unaryOp->s_reg = s_reg;

    this->AddInstruction(unaryOp);

    return unaryOp;
}

Instruction *Builder::LoadImmediate(const MachineSize value) {
    auto *instr = this->CreateObject<LoadImmValueInstr>(value);
    instr->dest.virtID = this->context->GetIncRVirtCounter();

    instr->value = value;

    this->AddInstruction(instr);

    return instr;
}

Module *Builder::CreateModule() noexcept {
    auto mod = this->allocator_.alloc<Module>(sizeof(Module));
    if (mod != nullptr) {
        new(mod) Module();

        this->context = &mod->context_;
    }

    return mod;
}

void Builder::AppendBasicBlock(BasicBlock *bb) const noexcept {
    assert(bb != nullptr);

    if (this->context->entry_ == nullptr) {
        this->context->entry_ = bb;
        this->context->current_ = bb;

        return;
    }

    this->context->current_->next = bb;
    bb->prev = this->context->current_;

    this->context->current_ = bb;
}

void Builder::AddToObjsList(Object *obj) const noexcept {
    obj->memory_.prev = this->context->objs;
    this->context->objs = obj;
}

void Builder::DeleteBasicBlock(BasicBlock *bb) const noexcept {
    this->RemoveFromObjsList(bb);

    bb->~BasicBlock();

    this->allocator_.free(bb);
}

void Builder::RemoveFromObjsList(Object *obj) const noexcept {
    auto *next = obj->memory_.next;
    auto *prev = obj->memory_.prev;

    obj->memory_.next = nullptr;
    obj->memory_.prev = nullptr;

    if (next != nullptr)
        next->memory_.prev = prev;

    if (prev == nullptr) {
        this->context->objs = next;

        return;
    }

    prev->memory_.next = next;
}
