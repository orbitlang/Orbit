// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <orbit/orbiter/opcode.h>

#include <orbit/orbiter/vm.h>

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
    IsolateAllocator allocator(isolate);

    vms->stack = allocator.alloc<Byte>(stackSize);
    if (vms->stack == nullptr)
        return false;

    vms->current = 0;
    vms->stackSize = stackSize;

    return true;
}

void *orbiter::eval(VMContext *vmc, Code *code) {
    auto *regs = &vmc->regs;
    auto *stack = &vmc->stack;

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
                assert(false);
        }

        NEXT_IP;
    }

    return nullptr;
}
