// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/vm.h>

#include <orbit/liftoff/codegen.h>

using namespace liftoff;
using namespace liftoff::ir;

// ============================================================================
// General Purpose Emit Macros
// ============================================================================

// Emit macro with opcode and immediate value
#define EMIT_IMMEDIATE(opcode, imm) \
    (((U32)(opcode) << 24) | (imm))

// Emit macro with opcode, flags, and offset
#define EMIT_FO(opcode, flags, offset) \
    (((U32)(opcode) << 24) | ((flags) << 16) | (offset))

// Emit macro specific to jump instructions with opcode and offset
#define EMIT_JMP(opcode, offset) \
    (((U32)(opcode) << 24) | (offset & 0xFFFFFF))

// ============================================================================
// Emit Macros with Destination and Immediate
// ============================================================================

// Emit macro with opcode, destination register, flags, and immediate value
#define EMIT_DFI(opcode, dst, flags, imm) \
    (((U32)(opcode) << 24) | ((dst) << 20) | ((flags) << 16) | (imm))

// Emit macro with opcode, destination register, source register, and immediate value
#define EMIT_DSI(opcode, dst, src, imm) \
    (((U32)(opcode) << 24) | ((dst) << 20) | ((src) << 16) | (imm))

// ============================================================================
// Emit Macros with Offset Variants
// ============================================================================

// Emit macro with opcode, flags, source register, and offset
#define EMIT_FSO(opcode, flags, src, offset) \
    (((U32)(opcode) << 24) | ((flags) << 20) | ((src) << 16) | (offset))

// Emit macro with opcode, source register, and offset (flags set to 0)
#define EMIT_SO(opcode, src, offset) \
    (((U32)(opcode) << 24) | ((0) << 20) | ((src) << 16) | (offset & 0xFFFF))

// Emit macro with opcode, destination register, and offset (source set to 0)
#define EMIT_DO(opcode, dst, offset) \
    (((U32)(opcode) << 24) | ((dst) << 20) | ((0) << 16) | (offset))

// ============================================================================
// Emit Macros with Destination, Source [, and Flags]
// ============================================================================

// Emit macro with opcode, destination register, source register, and flags
#define EMIT_DSF(opcode, dst, src, flags) \
    (((U32)(opcode) << 24) | ((dst) << 20) | ((src) << 16) | ((flags) << 12) & 0xF)

// Emit macro with opcode, destination register, source register, and flags
#define EMIT_DSO(opcode, dst, src, offset) \
    (((U32)(opcode) << 24) | ((dst) << 20) | ((src) << 16) | (offset & 0xFFFF))

// Emit macro with opcode, destination register, source register
#define EMIT_DS(opcode, dst, src) \
(((U32)(opcode) << 24) | ((dst) << 20) | ((src) << 16) | 0)

// ============================================================================
// Emit Macros with Dual Source Variants
// ============================================================================

// Emit macro with opcode, destination register, left and right source registers, and flags
#define EMIT_DSSF(opcode, dst, src_l, src_r, flags) \
    (((U32)(opcode) << 24) | ((dst) << 20) | ((src_l) << 16) | ((src_r) << 12) | ((flags) << 8))

// Emit macro with opcode, destination register, left and right source registers, and extended flags
#define EMIT_DSSFE(opcode, dst, src_l, src_r, flags) \
    (((U32)(opcode) << 24) | ((dst) << 20) | ((src_l) << 16) | ((src_r) << 12) | ((flags) & 0xFFF))

// Simplified Emit macro with opcode, destination register, left and right source registers (flags set to 0)
#define EMIT_DSS(opcode, dst, src_l, src_r) \
    EMIT_DSSF(opcode, dst, src_l, src_r, 0)

unsigned char *Codegen::EmitOpcodes(BasicBlock *block, unsigned char *m_code) {
    for (auto *cursor = block->instr.head; cursor != nullptr; cursor = cursor->next) {
        if (cursor->type() == ir::ObjectType::VIRT_INSTRUCTION)
            continue;

        const auto *instr = (PhysInstruction *) cursor;

        switch (instr->opcode) {
            case orbiter::OPCode::ADD:
            case orbiter::OPCode::SUB:
            case orbiter::OPCode::MUL:
            case orbiter::OPCode::DIV:
                *(orbiter::MachineWord *) m_code = EMIT_DSSF(instr->opcode,
                                                             instr->assigned_reg,
                                                             ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                             ((Instruction*)instr->operands[1].value)->assigned_reg,
                                                             ((ir::BinaryOpInstr*) instr)->flags);
                break;
            case orbiter::OPCode::AND:
            case orbiter::OPCode::OR:
            case orbiter::OPCode::XOR:
                *(orbiter::MachineWord *) m_code = EMIT_DSS(instr->opcode,
                                                            instr->assigned_reg,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            ((Instruction*)instr->operands[1].value)->assigned_reg);
                break;
            case orbiter::OPCode::SHLR:
            case orbiter::OPCode::SHRR:
                *(orbiter::MachineWord *) m_code = EMIT_DSF(instr->opcode,
                                                            instr->assigned_reg,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            ((ir::BinaryOpImmInstr*) instr)->flags);
                break;
            case orbiter::OPCode::SHLI:
            case orbiter::OPCode::SHRI:
                *(orbiter::MachineWord *) m_code = EMIT_DSI(instr->opcode,
                                                            instr->assigned_reg,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            ((ir::BinaryOpImmInstr*) instr)->imm);
                break;
            case orbiter::OPCode::MEMB:
            case orbiter::OPCode::CMP:
            case orbiter::OPCode::EQ:
                *(orbiter::MachineWord *) m_code = EMIT_DSSF(instr->opcode,
                                                             instr->assigned_reg,
                                                             ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                             ((Instruction*)instr->operands[1].value)->assigned_reg,
                                                             ((ir::BinaryOpInstr*) instr)->flags);
                break;
            case orbiter::OPCode::MVN:
            case orbiter::OPCode::NEG:
            case orbiter::OPCode::NOT:
                *(orbiter::MachineWord *) m_code = EMIT_DSF(instr->opcode,
                                                            instr->assigned_reg,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            0);
                break;
            case orbiter::OPCode::PANIC:
            case orbiter::OPCode::RET:
            case orbiter::OPCode::YLD:
                *(orbiter::MachineWord *) m_code = EMIT_SO(instr->opcode,
                                                           ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                           ((ReturnInstruction*)instr)->slots);
                break;
            case orbiter::OPCode::RETSUB:
                *(orbiter::MachineWord *) m_code = EMIT_SO(instr->opcode,
                                                           ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                           0);
                break;
            case orbiter::OPCode::CALL:
                *(orbiter::MachineWord *) m_code = EMIT_FSO(instr->opcode,
                                                            (U8)((ir::CallInstr*)instr)->mode,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            ((ir::CallInstr*)instr)->arguments);
                break;
            case orbiter::OPCode::EXECSUB:
                *(orbiter::MachineWord *) m_code = EMIT_SO(instr->opcode,
                                                           ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                           0);
                break;
            case orbiter::OPCode::LDCODE:
                *(orbiter::MachineWord *) m_code = EMIT_DO(instr->opcode,
                                                           instr->assigned_reg,
                                                           ((ir::OffsetInstruction*)instr)->offset);
                break;
            case orbiter::OPCode::LDCST:
            case orbiter::OPCode::LDIMM:
            case orbiter::OPCode::MKCLZ:
                *(orbiter::MachineWord *) m_code = EMIT_DFI(instr->opcode,
                                                            instr->assigned_reg,
                                                            ((ir::UnaryImmInstr *) instr)->flags, // Shift for LDCST
                                                            ((ir::UnaryImmInstr *) instr)->imm);
                break;
            case orbiter::OPCode::MOV:
            case orbiter::OPCode::MOWN:
                *(orbiter::MachineWord *) m_code = EMIT_DSF(instr->opcode,
                                                            instr->assigned_reg,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            0);
                break;
            case orbiter::OPCode::SETPROP:
                *(orbiter::MachineWord *) m_code = EMIT_DSO(instr->opcode,
                                                            ((ManipTypeInstruction*)instr->operands[0].value)->
                                                            assigned_reg,
                                                            ((ManipTypeInstruction*)instr->operands[1].value)->
                                                            assigned_reg,
                                                            ((ManipTypeInstruction*)instr)->offset);
                break;
            case orbiter::OPCode::NGBLV:
                *(orbiter::MachineWord *) m_code = EMIT_FSO(instr->opcode,
                                                            ((ir::OffsetInstruction *) instr)->flags,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            ((ir::OffsetInstruction *) instr)->offset);
                break;
            case orbiter::OPCode::STGBL:
            case orbiter::OPCode::STGOFF:
                *(orbiter::MachineWord *) m_code = EMIT_SO(instr->opcode,
                                                           ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                           ((ir::OffsetInstruction *) instr)->offset);
                break;
            case orbiter::OPCode::LDGBL:
            case orbiter::OPCode::LDGOFF:
            case orbiter::OPCode::SKLDR:
                *(orbiter::MachineWord *) m_code = EMIT_DSO(instr->opcode,
                                                            instr->assigned_reg,
                                                            ((ir::OffsetInstruction *) instr)->r_base,
                                                            ((ir::OffsetInstruction *) instr)->offset & 0xFFFF);
                break;
            case orbiter::OPCode::CLONEW:
            case orbiter::OPCode::NDICT:
            case orbiter::OPCode::NLIST:
            case orbiter::OPCode::NSET:
            case orbiter::OPCode::NTUPLE:
                *(orbiter::MachineWord *) m_code = EMIT_DO(instr->opcode,
                                                           instr->assigned_reg,
                                                           ((ir::UnaryImmInstr *) instr)->imm);
                break;
            case orbiter::OPCode::SKSTR:
                *(orbiter::MachineWord *) m_code = EMIT_DSO(instr->opcode,
                                                            ((ir::OffsetInstruction *) instr)->r_base,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            (((ir::OffsetInstruction *) instr)->offset & 0xFFFF));
                break;
            case orbiter::OPCode::PUSH:
                *(orbiter::MachineWord *) m_code = EMIT_SO(instr->opcode,
                                                           ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                           0);
                break;
            case orbiter::OPCode::POP:
                *(orbiter::MachineWord *) m_code = EMIT_DO(instr->opcode, 0, 0);
                break;
            case orbiter::OPCode::CLOLDR:
                *(orbiter::MachineWord *) m_code = EMIT_DFI(instr->opcode,
                                                            instr->assigned_reg,
                                                            (U8)((ir::LoadStoreClosureWithOffsetInstr*)instr)->mode,
                                                            ((ir::LoadStoreClosureWithOffsetInstr*)instr)->offset);
                break;
            case orbiter::OPCode::CLOSTR:
                *(orbiter::MachineWord *) m_code = EMIT_FSO(instr->opcode,
                                                            (U8)((ir::LoadStoreClosureWithOffsetInstr*)instr)->mode,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            ((ir::LoadStoreClosureWithOffsetInstr*)instr)->offset);
                break;
            case orbiter::OPCode::ALLOCA:
            case orbiter::OPCode::POPN:
                *(orbiter::MachineWord *) m_code = EMIT_FO(instr->opcode,
                                                           ((ir::UnaryImmInstr*)instr)->flags,
                                                           ((ir::UnaryImmInstr*)instr)->imm);
                break;
            case orbiter::OPCode::LDFUNC: {
                const auto *def_args = ((Instruction *) instr->operands[1].value);
                *(orbiter::MachineWord *) m_code = EMIT_DSSFE(instr->opcode,
                                                              instr->assigned_reg,
                                                              ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                              def_args!=nullptr ? def_args->assigned_reg:0,
                                                              ((ir::LoadFunc*)instr)->flags);
                break;
            }
            case orbiter::OPCode::ADDELEM:
                *(orbiter::MachineWord *) m_code = EMIT_DSS(instr->opcode,
                                                            ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                            ((Instruction*)instr->operands[1].value)->assigned_reg,
                                                            instr->num_ops > 2 ?
                                                            ((Instruction*)instr->operands[2].value)->assigned_reg
                                                            : 0);
                break;
            case orbiter::OPCode::LDINIT:
            case orbiter::OPCode::NOBJ:
                *(orbiter::MachineWord *) m_code = EMIT_DS(instr->opcode,
                                                           instr->assigned_reg,
                                                           ((Instruction*)instr->operands[0].value)->assigned_reg);
                break;
            case orbiter::OPCode::JEN:
            case orbiter::OPCode::JF:
            case orbiter::OPCode::JT: {
                const auto *jmp = (const BasicBlock *) (const Instruction *) instr->operands[1].value;

                assert(jmp->offset <= 0xFFFF);

                *(orbiter::MachineWord *) m_code = EMIT_SO(instr->opcode,
                                                           ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                           jmp->offset);
                break;
            }
            case orbiter::OPCode::JMP: {
                const auto *jmp = (const BasicBlock *) (const Instruction *) instr->operands[1].value;

                assert(jmp->offset <= 0xFFFFFF);

                *(orbiter::MachineWord *) m_code = EMIT_JMP(instr->opcode,
                                                            jmp->offset);
                break;
            }
            case orbiter::OPCode::SYNC_ENTER:
            case orbiter::OPCode::SYNC_EXIT:
                *(orbiter::MachineWord *) m_code = EMIT_SO(instr->opcode,
                                                           ((Instruction*)instr->operands[0].value)->assigned_reg,
                                                           0);
                break;
        }

        m_code += 4;
    }

    return m_code;
}

void Codegen::ExportSymbols(orbiter::datatype::HCode &code) {
    const auto *ir = this->ir_;

    if (ir->exported_names.empty())
        return;

    code->exported.length = ir->exported_names.size();
    code->exported.symbols = this->allocator_.alloc<orbiter::datatype::ExportedSymbol>(
        ir->exported_names.size() * sizeof(orbiter::datatype::ExportedSymbol)
    );

    if (code->exported.symbols == nullptr)
        throw std::bad_alloc();

    U16 index = 0;
    for (const auto &cursor: ir->exported_names) {
        auto *symbol = code->exported.symbols + index++;

        symbol->name = cursor.name.get_inc();
        symbol->flags = cursor.flags;
        symbol->slot = cursor.slot;
    }
}

orbiter::datatype::HCode Codegen::Generate() noexcept {
    const auto *ir = this->ir_;

    auto *m_code = this->allocator_.alloc<unsigned char>(ir->program_size);
    if (m_code == nullptr)
        return {};

    try {
        auto *m_cursor = m_code;
        for (auto *b_cursor = ir->entry_; b_cursor; b_cursor = b_cursor->next)
            m_cursor = EmitOpcodes(b_cursor, m_cursor);

        auto code = CodeNew(this->allocator_.GetIsolate(), m_code, ir->unknown_names.get(), ir->static_values.get(),
                            ir->program_size, ir->local_slots, ir->GetStackCount());
        if (!code)
            throw std::bad_alloc();

        this->ExportSymbols(code);

        code->SetProps(ir->name.get(), ir->doc.get());

        return code;
    } catch (...) {
        this->allocator_.free(m_code);
    }

    return {};
}
