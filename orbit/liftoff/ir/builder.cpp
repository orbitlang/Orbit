// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/atom.h>

#include <orbit/liftoff/ir/builder.h>

using namespace liftoff::ir;
using namespace orbiter;

BasicBlock *Builder::AddInstruction(Instruction *instruction) {
    auto *bb = this->context->current_;
    if (bb == nullptr)
        bb = this->CreateAppendBasicBlock();

    bb->AddInstruction(instruction);

    return bb;
}

PhysInstruction *Builder::GetLastInstructionMatch(OPCode opcode) const noexcept {
    if (this->context->current_ == nullptr)
        return nullptr;

    auto *instr = (PhysInstruction *) this->context->current_->instr.tail;

    if (instr == nullptr || instr->type() != ObjectType::INSTRUCTION || instr->opcode != opcode)
        return nullptr;

    return instr;
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
    if (this->context->current_ != nullptr
        && this->context->current_->instr.tail != nullptr
        && this->context->current_->instr.tail->objType_ == ObjectType::INSTRUCTION)
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

                this->context->stack_slots += slots;

                return last_alloca;
            }
        }
    }

    alloca = this->CreateObject<UnaryImmInstr>(OPCode::ALLOCA, (U8) flags, slots);

    if (last_alloca == nullptr) {
        if (bb_entry != nullptr)
            bb_entry->AddInstructionFirst(alloca);
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
    auto *call = this->CreateCallDetached(OPCode::CALL, src, arguments, mode);

    // this->StackDiscard(arguments); Managed by callee
    this->context->stack_push_count -= arguments;

    return call;
}

Instruction *Builder::CreateCallDetached(const OPCode opcode, Instruction *src, U16 arguments, CallMode mode) {
    return this->CreateObject<CallInstr>(opcode, src, arguments, mode);
}

Instruction *Builder::CreateCallNativeDetached(Instruction *src, const U16 arguments) {
    return this->CreateObject<CallInstr>(OPCode::NTCALL, src, arguments);
}

Instruction *Builder::CreateDec(Instruction *src) {
    return this->CreateInstruction<BinaryOpImmInstr>(OPCode::SUB, (U8) AddSubFlags::IMM8, src, 1);
}

Instruction *Builder::CreateError(Instruction *kind, Instruction *reason, Instruction *details) {
    return this->CreateInstruction<ErrorInstr>(kind, reason, details);
}

Instruction *Builder::CreateInc(Instruction *src) {
    return this->CreateInstruction<BinaryOpImmInstr>(OPCode::ADD, (U8) AddSubFlags::IMM8, src, 1);
}

Instruction *Builder::CreateIndexLoad(Instruction *target, Instruction *index) {
    return this->CreateInstruction<SubscrInstruction>(OPCode::LDIDX, target, index);
}

Instruction *Builder::CreateIndexStore(const SubscrInstruction *load, Instruction *value) {
    return this->CreateInstruction<SubscrInstruction>(OPCode::STIDX,
                                                      (Instruction *) load->operands[0].value,
                                                      (Instruction *) load->operands[1].value,
                                                      value);
}

Instruction *Builder::CreateJump(BasicBlock *destination) {
    auto *jmp = this->CreateInstruction<BranchInstruction>(OPCode::JMP, nullptr, destination);

    this->context->current_->alt = destination;

    return jmp;
}

Instruction *Builder::CreateJumpIfETypeMatch(Instruction *src, BasicBlock *catch_block) {
    if (src == nullptr)
        src = this->LoadNilValue();

    auto *jmp = this->CreateInstruction<BranchInstruction>(OPCode::JERR, src, catch_block);

    this->context->current_->alt = catch_block;

    return jmp;
}

Instruction *Builder::CreateManip(const OPCode opcode, Instruction *target, Instruction *src, Instruction *src1) {
    return this->CreateInstruction<ManipInstruction>(opcode, target, src, src1);
}

Instruction *Builder::CreateManip(const OPCode opcode, Instruction *target, Instruction *src) {
    return this->CreateInstruction<ManipInstruction>(opcode, target, src);
}

Instruction *Builder::CreateManipType(const OPCode opcode, Instruction *target, Instruction *src, U16 offset) {
    return this->CreateInstruction<ManipTypeInstruction>(opcode, target, src, offset);
}

Instruction *Builder::CreateStoreVariable(const OPCode opcode, I16 offset, U8 flags, Instruction *value) {
    auto *instr = this->CreateInstruction<OffsetInstruction>(opcode, 0, offset, value);

    instr->flags = flags;

    return instr;
}

Instruction *Builder::CreateSubscrLoad(Instruction *target, Instruction *start, Instruction *stop, Instruction *step) {
    return this->CreateInstruction<SubscrInstruction>(OPCode::LDSBSCR, target, start, stop, step);
}

Instruction *Builder::CreateSubscrStore(const SubscrInstruction *load, Instruction *value) {
    return this->CreateInstruction<SubscrInstruction>(OPCode::STSBSCR,
                                                      (Instruction *) load->operands[0].value,
                                                      (Instruction *) load->operands[1].value,
                                                      (Instruction *) load->operands[2].value,
                                                      (Instruction *) load->operands[3].value,
                                                      value);
}

Instruction *Builder::CreateReturn(Instruction *s_reg, const U16 slots) {
    auto *tcf = this->context->GetActiveContextIf(JBlockType::TCF);
    if (tcf != nullptr) {
        auto *ret = this->CreatePendingReturn(s_reg, slots);

        this->CreateJump(tcf->end);

        return ret;
    }

    if (this->context->deferred > 0) {
        if (s_reg->type() == ObjectType::INSTRUCTION
            && (((PhysInstruction *) s_reg)->opcode == OPCode::LDIMM
                || (((PhysInstruction *) s_reg)->opcode == OPCode::LDCST
                    && (((UnaryImmInstr *) s_reg)->flags == (U8) LoadConstantMode::FALSE
                        || ((UnaryImmInstr *) s_reg)->flags == (U8) LoadConstantMode::TRUE
                        || ((UnaryImmInstr *) s_reg)->flags == (U8) LoadConstantMode::NIL)))) {
            auto *execdefer = this->CreateObject<UnaryImmInstr>(OPCode::EXECDEFER, 0, 0);

            this->context->current_->AddInstructionBefore(s_reg, execdefer);

            return this->CreateInstruction<ReturnInstruction>(s_reg, slots);
        }

        const auto l_instr = this->FindAndCreateAppropriateLoad(s_reg);
        if (l_instr == nullptr) {
            if (this->context->deferred < 2) {
                this->AllocStackSlots(1, AllocaFlags::ZERO_INIT);

                this->context->deferred += 1;
            }

            const auto tmp_ret = (I16) (this->context->stack_slots - 1);

            this->StoreToStackOffset(s_reg, kBaseStackPointerReg, tmp_ret);

            this->CreateUnaryOp(OPCode::EXECDEFER);

            s_reg = this->LoadFromStackOffset(kBaseStackPointerReg, tmp_ret, true);

            return this->CreateInstruction<ReturnInstruction>(s_reg, slots);
        }

        this->CreateUnaryOp(OPCode::EXECDEFER);

        this->AddInstruction(l_instr);

        return this->CreateInstruction<ReturnInstruction>(l_instr, slots);
    }

    return this->CreateInstruction<ReturnInstruction>(s_reg, slots);
}

Instruction *Builder::CreateReturn(const U16 slots) {
    return this->CreateReturn(this->LoadNilValue(), slots);
}

Instruction *Builder::CreateReturnSub(Instruction *s_reg) {
    return this->CreateInstruction<ReturnSubInstruction>(s_reg);
}

Instruction *Builder::CreateUnaryOp(const OPCode opcode, Instruction *s_reg) {
    return this->CreateInstruction<UnaryOpInstr>(opcode, s_reg);
}

Instruction *Builder::CreateUnaryOp(const OPCode opcode, const U16 imm, const U8 flags) {
    return this->CreateInstruction<UnaryImmInstr>(opcode, flags, imm);
}

Instruction *Builder::CreateYield(Instruction *s_reg) {
    const auto *cursor = this->context->j_chain;

    while (cursor != nullptr) {
        if (cursor->type == JBlockType::SYNC)
            this->CreateUnaryOp(OPCode::SYNC_EXIT, cursor->value);

        cursor = cursor->prev;
    }

    cursor = this->context->j_chain;
    while (cursor != nullptr) {
        if (cursor->type == JBlockType::TCF)
            this->CreateUnaryOp(OPCode::TEND);

        cursor = cursor->prev;
    }

    auto *res = this->CreateInstruction<UnaryOpInstr>(OPCode::YLD, s_reg);

    cursor = this->context->j_chain;
    while (cursor != nullptr) {
        if (cursor->type == JBlockType::TCF)
            this->SetupTryCatch(cursor->alt, cursor->end);

        cursor = cursor->prev;
    }

    // Restore SYNC
    cursor = this->context->j_chain;
    while (cursor != nullptr) {
        if (cursor->type == JBlockType::SYNC)
            this->CreateUnaryOp(OPCode::SYNC_ENTER, cursor->value);

        cursor = cursor->prev;
    }

    return res;
}

Instruction *Builder::FindAndCreateAppropriateLoad(Instruction *src) {
    const OffsetInstruction *last = nullptr;

    const auto *cursor = (OffsetInstruction *) src;
    const Use *user = src->use_list;

    do {
        switch (cursor->opcode) {
            case OPCode::CLOSTR:
                last = this->CreateObject<OffsetInstruction>(OPCode::CLOLDR, cursor->r_base, cursor->offset);
                break;
            case OPCode::STGBL:
                last = this->CreateObject<OffsetInstruction>(OPCode::LDGBL, cursor->r_base, cursor->offset);
                break;
            case OPCode::STGOFF:
                last = this->CreateObject<OffsetInstruction>(OPCode::LDGOFF, cursor->r_base, cursor->offset);
                break;
            case OPCode::SKSTR:
                last = this->CreateObject<OffsetInstruction>(OPCode::SKLDR, cursor->r_base, cursor->offset);
                break;
            default:
                break;
        }

        if (user != nullptr) {
            cursor = (OffsetInstruction *) user->user;
            user = user->next;
        }
    } while (last == nullptr && user != nullptr);

    return (Instruction *) last;
}

Instruction *Builder::LoadAtomConstant(const char *string) {
    const auto str = datatype::AtomNew(this->context->isolate_, string);
    if (!str)
        throw std::bad_alloc();

    return this->LoadConstant((datatype::OObject *) str.get());
}

Instruction *Builder::LoadCodeObject(U16 offset) {
    return this->CreateInstruction<OffsetInstruction>(OPCode::LDCODE, offset);
}

Instruction *Builder::LoadConstant(U16 offset) {
    return this->LoadFromOffset(OPCode::LDCST, (I16) offset, 0);
}

Instruction *Builder::LoadConstant(datatype::OObject *object) {
    const auto offset = this->context->PushStaticValue(object);

    return this->LoadConstant(offset);
}

Instruction *Builder::LoadConstant(const char *string) {
    const auto str = datatype::ORStringNew(this->context->isolate_, string);
    if (!str)
        throw std::bad_alloc();

    return this->LoadConstant((datatype::OObject *) str.get());
}

Instruction *Builder::LoadClosureObject(U8 r_base, I16 offset) {
    return this->CreateInstruction<OffsetInstruction>(OPCode::LDCLO, r_base, offset);
}

Instruction *Builder::LoadExecCodeObject(U16 offset) {
    auto const co = this->LoadCodeObject(offset);

    return this->CreateInstruction<ExecSubInstr>(co);
}

Instruction *Builder::LoadFromStackOffset(U8 r_base, I16 offset, bool force_load) {
    if (force_load)
        return this->CreateInstruction<OffsetInstruction>(OPCode::SKLDR, r_base, offset);

    return this->LoadFromOffset(OPCode::SKLDR, r_base, offset, 0);
}

Instruction *Builder::LoadFunction(Instruction *src, Instruction *def_args, LoadFuncFlags flags) {
    return this->CreateInstruction<LoadFunc>(src, def_args, flags);
}

Instruction *Builder::LoadImmediate(const MachineSize value) {
    auto *instr = this->CreateInstruction<UnaryImmInstr>(OPCode::LDIMM, 0, value);
    // TODO: check size, use shift to load whole value

    return instr;
}

Instruction *Builder::LoadModule(datatype::ORString *path) {
    auto *ld_path = this->LoadConstant((datatype::OObject *) path);

    return this->LoadModule(ld_path);
}

Instruction *Builder::LoadModule(Instruction *src) {
    return this->CreateInstruction<UnaryOpInstr>(OPCode::LDMOD, src);
}

Instruction *Builder::LoadObjectProp(Instruction *src, U16 offset, bool as_key, bool super) {
    const auto flags = (as_key
                            ? LoadObjectPropFlags::KEY
                            : LoadObjectPropFlags::INLINE)
                       | (super ? LoadObjectPropFlags::SUPER : (LoadObjectPropFlags) 0);

    return this->CreateInstruction<LSObjectProp>(OPCode::LDOBJP, src, offset, flags);
}

Instruction *Builder::LoadFromOffset(const OPCode opcode, const U8 r_base, const I16 offset, const U8 flags) {
    const OffsetInstruction *last = nullptr;

    switch (opcode) {
        case OPCode::CLOLDR:
            last = (OffsetInstruction *) this->GetLastInstructionMatch(OPCode::CLOSTR);
            break;
        case OPCode::LDGBL:
            last = (OffsetInstruction *) this->GetLastInstructionMatch(OPCode::STGBL);
            break;
        case OPCode::LDGOFF:
            last = (OffsetInstruction *) this->GetLastInstructionMatch(OPCode::STGOFF);
            break;
        case OPCode::SKLDR:
            last = (OffsetInstruction *) this->GetLastInstructionMatch(OPCode::SKSTR);
            break;
        default:
            break;
    }

    if (last != nullptr
        && last->r_base == r_base
        && last->offset == offset
        && last->flags == flags)
        return (Instruction *) last->operands->value;

    auto *instr = this->CreateInstruction<OffsetInstruction>(opcode, r_base, offset);

    instr->flags = flags;

    return instr;
}

Instruction *Builder::SetupTryCatch(BasicBlock *catch_block, BasicBlock *finally_block) {
    if (catch_block == nullptr)
        return this->CreateInstruction<BranchInstruction>(OPCode::TBGIN, nullptr, nullptr);

    this->CreateInstruction<BranchInstruction>(OPCode::TBGIN, nullptr, catch_block);
    return this->CreateInstruction<BranchInstruction>(OPCode::TSFIN, nullptr, finally_block);
}

Instruction *Builder::StackDiscard(U16 slots) {
    if (slots == 0)
        return nullptr;

    this->context->stack_push_count -= slots;

    return this->CreateInstruction<UnaryImmInstr>(OPCode::POPN, 0, slots);
}

Instruction *Builder::GetStackPop() {
    this->context->stack_push_count -= 1;

    return this->CreateObject<UnaryOpInstr>(OPCode::POP);
}

Instruction *Builder::StackPop() {
    this->context->stack_push_count -= 1;

    return this->CreateInstruction<UnaryOpInstr>(OPCode::POP);
}

Instruction *Builder::GetStackPush(Instruction *s_reg) {
    this->UpdateStackSize();

    return this->CreateObject<UnaryOpInstr>(OPCode::PUSH, s_reg);
}

Instruction *Builder::StackPush(Instruction *s_reg) {
    this->UpdateStackSize();

    return this->CreateInstruction<UnaryOpInstr>(OPCode::PUSH, s_reg);
}

Instruction *Builder::StackPushIF(Instruction *s_reg, Instruction *target, Instruction *against,
                                  const PushIfFlags flags) {
    this->UpdateStackSize();

    return this->CreateInstruction<TernaryOpImmInstr>(OPCode::PUSHIF, s_reg, target, against, (U8) flags);
}


Instruction *Builder::GetStoreObjectProp(Instruction *obj, Instruction *value, U16 offset, bool as_key) {
    return this->CreateObject<LSObjectProp>(OPCode::STOBJP, obj, value, offset,
                                            as_key ? LoadObjectPropFlags::KEY : LoadObjectPropFlags::INLINE);
}

Instruction *Builder::StoreObjectProp(Instruction *obj, Instruction *value, U16 offset, bool as_key) {
    auto *store = this->GetStoreObjectProp(obj, value, offset, as_key);

    this->AddInstruction(store);

    return store;
}

Instruction *Builder::StoreToClosureAtOffset(Instruction *src, I16 offset) {
    return this->CreateInstruction<OffsetInstruction>(OPCode::CLOSTR, offset, src);
}

Instruction *Builder::StoreToStackOffset(Instruction *src, U8 r_base, I16 offset) {
    return this->CreateInstruction<OffsetInstruction>(OPCode::SKSTR, r_base, offset, src);
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

    this->context->current_ = bb;
}

void Builder::DeleteBasicBlock(BasicBlock *bb) const noexcept {
    this->context->RemoveFromObjList(bb);

    bb->~BasicBlock();

    this->allocator_.free(bb);
}

void Builder::LeaveContext() {
    /*
    bool changed = this->context->ComputeLiveness();

    while (changed)
        changed = this->context->ComputeLiveness();
    */

    this->context->stack_push_count -= this->context->deferred_stack_count;

    assert(this->context->stack_push_count ==0);

    if (this->context->back != nullptr)
        this->context = this->context->back;
}
