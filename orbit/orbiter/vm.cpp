// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <orbit/orbiter/opcode.h>

#include <orbit/orbiter/vm.h>

#include <orbit/orbiter/fiber.h>

using namespace orbiter;
using namespace orbiter::datatype;

bool orbiter::VMContextInit(VMContext *vmc, Isolate *isolate, MSize stackSize) noexcept {
    if (!VMStackInit(&vmc->stack, isolate, stackSize))
        return false;

    memory::MemoryZero(&vmc->regs, sizeof(Registers));

    vmc->state = VMState::RUNNABLE;

    return true;
}

bool orbiter::VMStackInit(VMStack *vms, Isolate *isolate, MSize stackSize) noexcept {
    memory::IsolateAllocator allocator(isolate);

    vms->stack = allocator.alloc<Byte>(stackSize);
    if (vms->stack == nullptr)
        return false;

    vms->current = 0;
    vms->stackSize = stackSize;

    return true;
}

void *orbiter::eval(Fiber *fiber) {
    auto *regs = &fiber->vm.regs;
    auto *stack = &fiber->vm.stack;

#define TARGET_OP(op)   case OPCode::op:
#define CGOTO           continue
#define DISPATCH        \
NEXT_IP;                \
CGOTO

#define FETCH                   (*((MachineWord *) regs->IP.reg))
#define FETCH_OP(instr)         ((OPCode) (instr >> 24))

#define NEXT_IP                 (regs->IP.reg += sizeof(MachineWord))

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
        module_slots = O_SLOT(fiber->context.module);

    while (1) {
        const auto instr = FETCH;

        switch (FETCH_OP(instr)) {
            TARGET_OP(LDCST) {
                break;
            }
            TARGET_OP(LDIMM) {
                const auto imm = instr & 0xFFFFu;
                const auto shift = (instr >> 16) & 0x0F;
                const auto dst = FETCH_R_DST(instr);

                REG_N(dst) = REG_N(dst) | (imm << (16 * shift));

                DISPATCH;
            }
            TARGET_OP(STGBL) {
                const auto value = REG_N(FETCH_R_SRC(instr));
                const auto k_index = FETCH_IMM(instr);

                ContextSet(fiber->context.context, (ORString *) code->unknown_symbols->objects[k_index],
                           (OObject *) value);

                DISPATCH;
            }
            TARGET_OP(STGOFF) {
                const auto value = REG_N(FETCH_R_SRC(instr));
                const auto slot = FETCH_IMM(instr);

                assert(module_slots != nullptr);

                *(module_slots + slot) = O_VFY_INCREF((OObject *) value);

                DISPATCH;
            }
            TARGET_OP(LDGOFF) {
                const auto r_dest = FETCH_R_DST(instr);
                const auto slot = FETCH_IMM(instr);

                assert(module_slots != nullptr);

                REG_N(r_dest) = (PtrSize) *(module_slots + slot);

                DISPATCH;
            }
            TARGET_OP(MOV) {
                const auto src = FETCH_R_SRC(instr);
                const auto dst = FETCH_R_DST(instr);

                REG_N(dst) = (PtrSize) O_VFY_INCREF((OObject*)REG_N(src));

                DISPATCH;
            }
            TARGET_OP(MOWN) {
                const auto src = FETCH_R_SRC(instr);

                REG_N(FETCH_R_DST(instr)) = REG_N(src);
                REG_N(src) = 0;

                DISPATCH;
            }
            default:
                DISPATCH;
        }

        NEXT_IP;
    }

    return nullptr;
}
