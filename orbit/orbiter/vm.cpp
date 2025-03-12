// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/opcode.h>
#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/vm.h>

using namespace orbiter;
using namespace orbiter::datatype;

OObject *orbiter::eval(Fiber *fiber) {
    auto *regs = &fiber->vm.regs;
    auto *stack = &fiber->vm.stack;

    fiber->vm.state = VMState::RUNNING;

#define TARGET_OP(op)   case OPCode::op:
#define CGOTO           continue
#define DISPATCH        \
NEXT_IP;                \
CGOTO

#define FETCH                   (*((MachineWord *) regs->IP.reg))
#define FETCH_OP(instr)         ((OPCode) (instr >> 24))

#define NEXT_IP                 (regs->IP.reg += sizeof(MachineWord))
#define JMP_TO(offset)          (regs->IP.reg = (PtrSize) code->m_code + offset)

#define REGISTER(registers, n)  (*((PtrSize *) (((Register *) (registers)) + n)))

#define REG_N(n)                REGISTER(regs, n)
#define REG_RR                  REG_N(13)
#define REG_BP                  REG_N(14)
#define REG_SP                  REG_N(15)

#define FETCH_R_DST(instr)      ((instr >> 20) & 0xFu)
#define FETCH_R_SRC(instr)      ((instr >> 16) & 0xFu)
#define FETCH_IMM(instr)        ((instr) & 0xFFFFu)

    auto *code = fiber->context.code;

    OObject **module_slots = nullptr;
    if (fiber->context.module != nullptr)
        module_slots = O_SLOT(fiber->context.module, O_GET_TYPE(fiber->context.module));

    while (regs->IP.reg < (PtrSize) code->m_end) {
        const auto instr = FETCH;

        switch (FETCH_OP(instr)) {
            TARGET_OP(EQ) {
                const auto dst = FETCH_R_DST(instr);
                const auto src_l = (instr >> 16) & 0xFu;
                const auto src_r = (instr >> 12) & 0xFu;
                const auto flags = (EqualityMode) ((instr >> 8) & 0xFu);
                bool res;

                if (flags == EqualityMode::NORMAL)
                    res = Equal((const OObject *) REG_N(src_l), (const OObject *) REG_N(src_r));
                else
                    res = EqualStrict((const OObject *) REG_N(src_l), (const OObject *) REG_N(src_r));

                REG_N(dst) = BOOL_TO_OBOOL(res);

                DISPATCH;
            }
            TARGET_OP(LDCST) {
                const auto dst = FETCH_R_DST(instr);
                const auto flags = (LoadConstantMode) FETCH_R_SRC(instr);
                const auto imm = FETCH_IMM(instr);

                if (flags == LoadConstantMode::OFFSET)
                    REG_N(dst) = (PtrSize) code->static_resources->objects[imm];
                else if (flags == LoadConstantMode::TRUE)
                    REG_N(dst) = kOddBallTRUE;
                else if (flags == LoadConstantMode::FALSE)
                    REG_N(dst) = kOddBallFALSE;
                else if (flags == LoadConstantMode::NIL)
                    REG_N(dst) = (PtrSize) kOddBallNIL;

                DISPATCH;
            }
            TARGET_OP(LDIMM) {
                const auto imm = FETCH_IMM(instr);
                const auto shift = (instr >> 16) & 0x0F;
                const auto dst = FETCH_R_DST(instr);

                REG_N(dst) = REG_N(dst) | (imm << (16 * shift));

                DISPATCH;
            }
            TARGET_OP(STGBL) {
                const auto value = REG_N(FETCH_R_SRC(instr));
                const auto k_index = FETCH_IMM(instr);

                ContextSet(fiber->context.context,
                           (ORString *) code->unknown_symbols->objects[k_index],
                           (OObject *) value);

                DISPATCH;
            }
            TARGET_OP(STGOFF) {
                const auto value = REG_N(FETCH_R_SRC(instr));
                const auto slot = FETCH_IMM(instr);

                assert(module_slots != nullptr);

                *(module_slots + slot) = (OObject *) value;

                DISPATCH;
            }
            TARGET_OP(LDGOFF) {
                const auto r_dest = FETCH_R_DST(instr);
                const auto slot = FETCH_IMM(instr);

                assert(module_slots != nullptr);

                REG_N(r_dest) = (PtrSize) *(module_slots + slot);

                DISPATCH;
            }
            TARGET_OP(JEN) {
                const auto src = FETCH_R_SRC(instr);
                const auto offset = FETCH_IMM(instr);

                if (REG_N(src) == (PtrSize) kOddBallNIL) {
                    JMP_TO(offset);

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(JF) {
                const auto src = FETCH_R_SRC(instr);
                const auto offset = FETCH_IMM(instr);

                if (REG_N(src) == kOddBallFALSE) {
                    JMP_TO(offset);

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(JT) {
                const auto src = FETCH_R_SRC(instr);
                const auto offset = FETCH_IMM(instr);

                if (REG_N(src) == kOddBallTRUE) {
                    JMP_TO(offset);

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(JMP) {
                const auto offset = FETCH_IMM(instr);

                JMP_TO(offset);
                continue;
            }
            default:
                DISPATCH;
        }

        NEXT_IP;
    }

    return (OObject *) regs->RR.reg;
}
