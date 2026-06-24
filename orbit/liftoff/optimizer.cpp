// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/liftoff/optimizer.h>

using namespace liftoff;
using namespace liftoff::ir;

bool Optimizer::HasSideEffects(const orbiter::OPCode opcode) {
    switch (opcode) {
        // Function calls & execution
        case orbiter::OPCode::CALL:
        case orbiter::OPCode::NTCALL:
        case orbiter::OPCode::EXECSUB:
        case orbiter::OPCode::EXECDEFER:
        case orbiter::OPCode::SPWN:
        case orbiter::OPCode::DEFER:
        case orbiter::OPCode::AWAIT:

        // Control flow & returns
        case orbiter::OPCode::RET:
        case orbiter::OPCode::RETSUB:
        case orbiter::OPCode::YLD:
        case orbiter::OPCode::PANIC:
        case orbiter::OPCode::JMP:
        case orbiter::OPCode::JT:
        case orbiter::OPCode::JF:
        case orbiter::OPCode::JEN:
        case orbiter::OPCode::JERR:
        case orbiter::OPCode::JEXH:

        // Stores
        case orbiter::OPCode::STGBL:
        case orbiter::OPCode::STGOFF:
        case orbiter::OPCode::STRES:
        case orbiter::OPCode::STOBJP:
        case orbiter::OPCode::STIDX:
        case orbiter::OPCode::STSBSCR:
        case orbiter::OPCode::LDCLO:
        case orbiter::OPCode::CLOSTR:
        case orbiter::OPCode::SKSTR:
        case orbiter::OPCode::SETPROP:

        // Stack manipulation
        case orbiter::OPCode::PUSH:
        case orbiter::OPCode::PUSHIF:
        case orbiter::OPCode::POP:
        case orbiter::OPCode::POPN:
        case orbiter::OPCode::ALLOCA:

        // Global variable creation
        case orbiter::OPCode::NGBLV:

        // Iterator
        case orbiter::OPCode::GITR:
        case orbiter::OPCode::ITRNXT:

        // Class / trait
        case orbiter::OPCode::MKCLZ:
        case orbiter::OPCode::MKTRT:

        // Container mutation
        case orbiter::OPCode::ADDELEM:

        // Channel operations
        case orbiter::OPCode::CHSND:
        case orbiter::OPCode::CHRCV:

        // Sync
        case orbiter::OPCode::SYNC_ENTER:
        case orbiter::OPCode::SYNC_EXIT:

        // Try/catch/finally infrastructure
        case orbiter::OPCode::TBGIN:
        case orbiter::OPCode::TEND:
        case orbiter::OPCode::TSFIN:
        case orbiter::OPCode::TSPA:
        case orbiter::OPCode::LDEXC:
            return true;

        default:
            return false;
    }
}

void Optimizer::DeadCodeElimination() const {
    for (const auto *block = this->ir_->entry_; block != nullptr; block = block->next) {
        auto *instr = block->instr.tail;

        while (instr != nullptr) {
            auto *prev = instr->prev;

            if (instr->use_list == nullptr
                && instr->type() == ObjectType::INSTRUCTION
                && !HasSideEffects(((PhysInstruction *) instr)->opcode))
                this->ir_->DeleteInstruction(instr);

            instr = prev;
        }
    }
}

void Optimizer::Optimize() {
    this->DeadCodeElimination();

    // TODO: level ...
}
