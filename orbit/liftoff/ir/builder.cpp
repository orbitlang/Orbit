// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/ir/builder.h>

using namespace liftoff::ir;
using namespace orbiter;

BasicBlock *Builder::AddInstruction(Instruction *instruction) noexcept {
    auto *bb = this->context_->entry_;

    if (bb == nullptr) {
        if ((bb = this->allocator_.AllocObject<BasicBlock>()) == nullptr)
            return nullptr;

        this->context_->entry_ = bb;
    }

    if (bb->instr.head == nullptr) {
        bb->instr.head = instruction;
        bb->instr.tail = instruction;
    } else {
        instruction->prev = bb->instr.tail;
        bb->instr.tail->next = instruction;
        bb->instr.tail = instruction;
    }

    return bb;
}

Instruction *Builder::CreateBinaryOp(OPCode opcode, Object *left, Object *right) noexcept {
    auto binOp = this->allocator_.alloc<BinaryOpInstr>(sizeof(BinaryOpInstr));
    if (binOp != nullptr) {
        new(binOp)BinaryOpInstr(opcode);

        binOp->dest.virtID = this->context_->GetIncCounter();

        binOp->left = left;
        binOp->right = right;

        this->AddInstruction(binOp);
    }

    return binOp;
}


Instruction *Builder::CreateBinaryOpFlags(OPCode opcode, U8 flags, Object *left, Object *right) noexcept {
    auto binOp = this->allocator_.alloc<BinaryOpFlagsInstr>(sizeof(BinaryOpInstr));
    if (binOp != nullptr) {
        new(binOp)BinaryOpFlagsInstr(opcode, flags);

        binOp->dest.virtID = this->context_->GetIncCounter();

        binOp->left = left;
        binOp->right = right;

        this->AddInstruction(binOp);
    }

    return binOp;
}

Instruction *Builder::LoadFromStackOffset(unsigned short offset) noexcept {
    auto instr = this->allocator_.alloc<StackLoadInstr>(sizeof(StackLoadInstr));
    if (instr != nullptr) {
        new(instr) StackLoadInstr();

        instr->dest.virtID = this->context_->GetIncCounter();
        instr->offset = offset;

        this->AddInstruction(instr);
    }

    return instr;
}

Instruction *Builder::LoadImmediate(MachineSize value) noexcept {
    auto instr = this->allocator_.alloc<LoadImmValueInstr>(sizeof(LoadImmValueInstr));
    if (instr != nullptr) {
        new(instr) LoadImmValueInstr();

        instr->dest.virtID = this->context_->GetIncCounter();

        instr->value = value;

        this->AddInstruction(instr);
    }

    return instr;
}

Module *Builder::CreateModule() noexcept {
    auto mod = this->allocator_.alloc<Module>(sizeof(Module));
    if (mod != nullptr) {
        new(mod) Module();

        this->context_ = &mod->context_;
    }

    return mod;
}

Value *Builder::CreateImmediateValue(U16 value) noexcept {
    auto r_value = this->allocator_.alloc<Value>(sizeof(Value));
    if (r_value != nullptr)
        new(r_value) Value(value);

    return r_value;
}
