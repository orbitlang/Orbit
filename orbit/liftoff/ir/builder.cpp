// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/liftoff/ir/builder.h>

using namespace liftoff::ir;
using namespace orbiter;

BasicBlock *Builder::AddInstruction(Instruction *instruction) {
    auto *bb = this->context->current_;
    if (bb == nullptr)
        bb = this->CreateAppendBasicBlock();

    bb->AddInstruction(instruction);

    this->context->program_size += 4;

    return bb;
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

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

Builder::~Builder() noexcept {
    if (this->context == nullptr || !this->delete_context_)
        return;

    auto *first = this->context;
    while (first->back != nullptr)
        first = first->back;

    IRContext::Delete(first);
}

BasicBlock *Builder::CreateAppendBasicBlock() {
    auto *bb = this->CreateBasicBlock();

    this->AppendBasicBlock(bb);

    return bb;
}

bool Builder::CheckIfLastInstructionIs(OPCode opcode) const {
    if (this->context->current_->instr.tail->objType_ == ObjectType::INSTRUCTION)
        return ((PhysInstruction *) this->context->current_->instr.tail)->opcode == opcode;

    return false;
}

Instruction *Builder::AllocStackSlots(U16 slots, AllocaFlags flags) {
    const auto bb_entry = this->context->entry_;

    UnaryImmInstr *alloca = nullptr;
    UnaryImmInstr *last_alloca = nullptr;

    if (bb_entry != nullptr) {
        for (auto cursor = bb_entry->instr.head; cursor != nullptr; cursor = cursor->next) {
            if (cursor->type() == ObjectType::INSTRUCTION) {
                auto *instr = (PhysInstruction *) cursor;

                if (instr->opcode == OPCode::ALLOCA) {
                    last_alloca = (UnaryImmInstr *) instr;
                    continue;
                }
            }

            break;
        }

        if (last_alloca != nullptr) {
            if ((AllocaFlags) last_alloca->flags == flags) {
                last_alloca->imm += slots;

                return last_alloca;
            }
        }
    }

    alloca = this->CreateObject<UnaryImmInstr>(OPCode::ALLOCA, (U8) flags, slots);

    if (last_alloca == nullptr) {
        if (bb_entry != nullptr) {
            bb_entry->AddInstructionFirst(alloca);

            this->context->program_size += 4;
        }
        else
            this->AddInstruction(alloca);
    } else
        IRContext::InsertInstructionAfter(last_alloca, alloca);


    this->context->stack_slots += slots;

    return alloca;
}

Instruction *Builder::CreateBinaryOp(const OPCode opcode, Instruction *left, Instruction *right) {
    return this->CreateInstruction<BinaryOpInstr>(opcode, left, right);
}

Instruction *Builder::CreateBinaryOpFlags(const OPCode opcode, const U8 flags, Instruction *left, Instruction *right) {
    return this->CreateInstruction<BinaryOpInstr>(opcode, flags, left, right);
}

Instruction *Builder::CreateBinaryOpFlags(const OPCode opcode, const U8 flags, Instruction *left, U16 right) {
    return this->CreateObject<BinaryOpImmInstr>(opcode, flags, left, right);
}

Instruction *Builder::CreateBranch(const OPCode opcode, Instruction *value, BasicBlock *continuation,
                                   BasicBlock *destination) {
    auto *branch = this->CreateInstruction<BranchInstruction>(opcode, value, destination);

    this->context->current_->alt = destination;

    if (continuation == nullptr)
        this->CreateAppendBasicBlock();
    else
        this->AppendBasicBlock(continuation);

    return branch;
}

Instruction *Builder::CreateCall(Instruction *src, U16 arguments, CallMode mode) {
    auto *call = this->CreateInstruction<CallInstr>(src, arguments, mode);

    // this->StackDiscard(arguments); Managed by callee
    this->context->stack_push_count -= arguments;

    return call;
}

Instruction *Builder::CreateJump(BasicBlock *destination) {
    auto *jmp = this->CreateInstruction<BranchInstruction>(OPCode::JMP, nullptr, destination);

    this->context->current_->alt = destination;

    return jmp;
}

Instruction *Builder::CreateManip(const OPCode opcode, Instruction *target, Instruction *src, Instruction *src1) {
    return this->CreateInstruction<ManipInstruction>(opcode, target, src, src1);
}

Instruction *Builder::CreateManip(const OPCode opcode, Instruction *target, Instruction *src) {
    return this->CreateInstruction<ManipInstruction>(opcode, target, src);
}

Instruction *Builder::CreateStoreVariable(const OPCode opcode, I16 offset, U8 flags, Instruction *value) {
    auto *instr = this->CreateInstruction<OffsetInstruction>(opcode, offset, value);

    instr->flags = flags;

    return instr;
}

Instruction *Builder::CreateReturn(Instruction *s_reg, U16 slots) {
    return this->CreateInstruction<ReturnInstruction>(s_reg, slots);
}

Instruction *Builder::CreateReturn(U16 slots) {
    return this->CreateInstruction<ReturnInstruction>(this->LoadNilValue(), slots);
}

Instruction *Builder::CreateUnaryOp(const OPCode opcode, Instruction *s_reg) {
    return this->CreateInstruction<UnaryOpInstr>(opcode, s_reg);
}

Instruction *Builder::CreateUnaryOp(const OPCode opcode, U16 imm, U8 flags) {
    return this->CreateInstruction<UnaryImmInstr>(opcode, flags, imm);
}

Instruction *Builder::LoadCodeObject(U16 offset) {
    return this->CreateInstruction<OffsetInstruction>(OPCode::LDCODE, offset);
}

Instruction *Builder::LoadConstant(U16 offset) {
    return this->LoadFromOffset(OPCode::LDCST, (I16) offset, 0);
}

Instruction *Builder::LoadFromClosureAtOffset(I16 offset, ClosureLSMode mode) {
    return this->CreateInstruction<LoadStoreClosureWithOffsetInstr>(OPCode::CLOLDR, offset, mode, nullptr);
}

Instruction *Builder::LoadFromStackOffset(I16 offset) {
    return this->LoadFromOffset(OPCode::SKLDR, offset, 0);
}

Instruction *Builder::LoadFunction(Instruction *src, Instruction *def_args, LoadFuncFlags flags) {
    return this->CreateInstruction<LoadFunc>(src, def_args, flags);
}

Instruction *Builder::LoadImmediate(const MachineSize value) {
    auto *instr = this->CreateInstruction<UnaryImmInstr>(OPCode::LDIMM, 0, value);
    // TODO: check size, use shift to load whole value

    return instr;
}

Instruction *Builder::LoadFromOffset(const OPCode opcode, const I16 offset, U8 flags) {
    auto *instr = this->CreateInstruction<OffsetInstruction>(opcode, offset);

    instr->flags = flags;

    return instr;
}

Instruction *Builder::StackDiscard(U16 slots) {
    if (slots == 0)
        return nullptr;

    this->context->stack_push_count -= slots;

    return this->CreateInstruction<UnaryImmInstr>(OPCode::POPN, 0, slots);
}

Instruction *Builder::StackPop() {
    this->context->stack_push_count -= 1;

    return this->CreateInstruction<UnaryOpInstr>(OPCode::POP);
}

Instruction *Builder::StackPush(Instruction *s_reg) {
    this->context->stack_push_count += 1;

    if (this->context->stack_push_count > this->context->stack_push_max)
        this->context->stack_push_max = this->context->stack_push_count;

    return this->CreateInstruction<UnaryOpInstr>(OPCode::PUSH, s_reg);
}

Instruction *Builder::StoreToClosureAtOffset(Instruction *src, I16 offset, ClosureLSMode mode) {
    return this->CreateInstruction<LoadStoreClosureWithOffsetInstr>(OPCode::CLOSTR, offset, mode, src);
}

Instruction *Builder::StoreToStackOffset(Instruction *src, I16 offset) {
    return this->CreateInstruction<OffsetInstruction>(OPCode::SKSTR, offset, src);
}

PhiInstr *Builder::CreatePhi() {
    return this->CreateInstruction<PhiInstr>();
}

U16 Builder::IRContextNew(IRContextType type, U16 local_slots) {
    auto *ictx = this->allocator_.alloc<IRContext>(sizeof(IRContext));
    if (ictx == nullptr)
        throw std::bad_alloc();

    new(ictx)IRContext(this->allocator_.GetIsolate(), type);

    ictx->local_slots = local_slots;

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

void Builder::AppendBasicBlock(BasicBlock *bb) const noexcept {
    assert(bb != nullptr);

    if (this->context->entry_ == nullptr) {
        this->context->entry_ = bb;
        this->context->current_ = bb;

        return;
    }

    this->context->current_->next = bb;
    bb->prev = this->context->current_;

    bb->offset = this->context->current_->offset + this->context->current_->size;

    this->context->current_ = bb;
}

void Builder::DeleteBasicBlock(BasicBlock *bb) const noexcept {
    this->RemoveFromObjsList(bb);

    bb->~BasicBlock();

    this->allocator_.free(bb);
}

void Builder::LeaveContext() {
    /*
    bool changed = this->context->ComputeLiveness();

    while (changed)
        changed = this->context->ComputeLiveness();
    */

    assert(this->context->stack_push_count ==0);

    if (this->context->back != nullptr)
        this->context = this->context->back;
}
