// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/liftoff/ir/builder.h>

using namespace liftoff::ir;
using namespace orbiter;

Instruction *Builder::LoadImmConstant(LoadConstantMode mode) {
    auto *instr = this->CreateObject<UnaryImmInstr>(OPCode::LDIMM, (U8) mode);

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::LoadStoreOffset(const OPCode opcode, const I16 offset) {
    auto *instr = this->CreateObject<OffsetInstruction>(opcode, offset);

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::AllocStackSlots(U16 slots, AllocaFlags flags) {
    auto *instr = this->CreateObject<UnaryImmInstr>(OPCode::ALLOCA, (U8)flags, slots);

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::CreateBinaryOp(const OPCode opcode, Instruction *left, Instruction *right) {
    auto *instr = this->CreateObject<BinaryOpInstr>(opcode, left, right);

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::CreateBinaryOpFlags(const OPCode opcode, const U8 flags, Instruction *left, Instruction *right) {
    auto *binOp = this->CreateObject<BinaryOpInstr>(opcode, flags, left, right);

    this->AddInstruction(binOp);

    return binOp;
}

Instruction *Builder::CreateBinaryOpFlags(const OPCode opcode, const U8 flags, Instruction *left, U16 right) {
    auto *binOp = this->CreateObject<BinaryOpImmInstr>(opcode, flags, left, right);

    this->AddInstruction(binOp);

    return binOp;
}

Instruction *Builder::CreateBranch(const OPCode opcode, Instruction *value, BasicBlock *continuation,
                                   BasicBlock *destination) {
    auto *branch = this->CreateObject<BranchInstruction>(opcode, value, destination);

    this->AddInstruction(branch);

    this->context->current_->alt = destination;

    if (continuation == nullptr)
        this->CreateAppendBasicBlock();
    else
        this->AppendBasicBlock(continuation);

    return branch;
}

Instruction *Builder::CreateCall(Instruction *src, U16 arguments) {
    auto *instr = this->CreateObject<CallInstr>(src, arguments);

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::CreateJump(BasicBlock *destination) {
    auto *jmp = this->CreateObject<BranchInstruction>(OPCode::JMP, nullptr, destination);

    this->AddInstruction(jmp);

    this->context->current_->alt = destination;

    return jmp;
}

Instruction *Builder::CreateUnaryOp(const OPCode opcode, Instruction *s_reg) {
    auto *unaryOp = this->CreateObject<UnaryOpInstr>(opcode, s_reg);

    this->AddInstruction(unaryOp);

    return unaryOp;
}

Instruction *Builder::CreateUnaryOp(const OPCode opcode, U16 imm, U8 flags) {
    auto *unaryOp = this->CreateObject<UnaryImmInstr>(opcode, flags, imm);

    this->AddInstruction(unaryOp);

    return unaryOp;
}

Instruction *Builder::LoadFunction(Instruction *src, LoadFuncFlags flags) {
    auto *instr = this->CreateObject<UnaryOpInstr>(OPCode::LDFUNC, (U8) flags,src);

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::LoadImmediate(const MachineSize value) {
    auto *instr = this->CreateObject<LoadImmValueInstr>(value);
    // TODO: check size, use shift to load whole value

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::CreateReturn(Instruction *s_reg, bool yield) {
    auto *instr = this->CreateObject<ReturnInstruction>(s_reg, yield);

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::StackPop() {
    auto *instr = this->CreateObject<UnaryOpInstr>(OPCode::POP);

    this->AddInstruction(instr);

    return instr;
}

Instruction *Builder::StackPush(Instruction *s_reg) {
    auto *instr = this->CreateObject<UnaryOpInstr>(OPCode::PUSH, s_reg);

    this->AddInstruction(instr);

    return instr;
}

U16 Builder::IRContextNew(IRContextType type) {
    auto *ictx = this->allocator_.alloc<IRContext>(sizeof(IRContext));
    if (ictx == nullptr)
        throw std::bad_alloc();

    new(ictx)IRContext(this->isolate_, type);

    U16 r_id = 0;

    if (this->context != nullptr) {
        try {
            r_id = this->context->PushSubContext(ictx);
        } catch (...) {
            ictx->~IRContext();

            this->allocator_.free(ictx);

            throw;
        }
    }

    if (this->context != nullptr)
        this->context->InvalidateActiveVar(nullptr);

    this->context = ictx;

    return r_id;
}

PhiInstr *Builder::CreatePhi() {
    auto *phi = this->CreateObject<PhiInstr>();

    this->AddInstruction(phi);

    return phi;
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
    obj->memory_.prev = this->context->objs_;
    this->context->objs_ = obj;
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
        this->context->objs_ = next;

        return;
    }

    prev->memory_.next = next;
}
