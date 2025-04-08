// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/opcode.h>
#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/vm.h>

using namespace orbiter;
using namespace orbiter::datatype;

constexpr auto kSkOffset = sizeof(FiberContext) + (sizeof(void *) * 2);

HOObject VMAdd(Isolate *isolate, PtrSize left, PtrSize right) {
    if (O_IS_SMI(left) && O_IS_SMI(right)) {
        left -= 1;

        const auto res = left + right;
        if (res >= kSMIMaxSize)
            return (HOObject) IntNew(isolate, (((IntegerUnderlying) left) >> 1) + (((IntegerUnderlying) right) >> 1));

        return HOObject((OObject *) res);
    }

    // TODO: NON SMI, Other object
    assert(false);

    return {};
}

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

#define REG_IP                  (regs->IP.reg)

#define FETCH_R_DST(instr)      ((instr >> 20) & 0xFu)
#define FETCH_R_SRC(instr)      ((instr >> 16) & 0xFu)
#define FETCH_R_RSRC(instr)     ((instr >> 12) & 0xFu)
#define FETCH_IMM(instr)        ((instr) & 0xFFFFu)

#define ACCESS_STACK_BP(offset) ((PtrSize *)(stack->stack + (regs->BP.reg + (offset))))
#define ACCESS_STACK_SP(offset) ((PtrSize *)(stack->stack + (regs->SP.reg + (offset))))
#define LOAD_FROM_STACK         ACCESS_STACK_SP((-sizeof(void *)))

    auto *code = fiber->context.code;

    OObject **module_slots = nullptr;
    if (fiber->context.module != nullptr)
        module_slots = O_SLOT(fiber->context.module, O_GET_TYPE(fiber->context.module));

    while (regs->IP.reg < (PtrSize) code->m_end) {
        const auto instr = FETCH;

        switch (FETCH_OP(instr)) {
            TARGET_OP(ADD) {
                const auto dst = FETCH_R_DST(instr);
                const auto src_l = FETCH_R_SRC(instr);
                const auto src_r = FETCH_R_RSRC(instr);

                REG_N(dst) = (PtrSize) VMAdd(fiber->isolate, REG_N(src_l), REG_N(src_r)).get();

                DISPATCH;
            }
            TARGET_OP(EQ) {
                const auto dst = FETCH_R_DST(instr);
                const auto src_l = FETCH_R_SRC(instr);
                const auto src_r = FETCH_R_RSRC(instr);
                const auto flags = (EqualityMode) ((instr >> 8) & 0xFu);
                bool res;

                if (flags == EqualityMode::NORMAL)
                    res = Equal((const OObject *) REG_N(src_l), (const OObject *) REG_N(src_r));
                else
                    res = EqualStrict((const OObject *) REG_N(src_l), (const OObject *) REG_N(src_r));

                REG_N(dst) = BOOL_TO_OBOOL(res);

                DISPATCH;
            }
            TARGET_OP(RET) {
                const auto src = FETCH_R_SRC(instr);

                // FIXME: Cleanup local variables!

                REG_RR = REG_N(src);

                REG_BP -= sizeof(void *);
                REG_IP = *ACCESS_STACK_BP(0) + sizeof(MachineWord);

                REG_BP -= sizeof(void *);
                REG_SP = REG_BP;

                REG_BP = *ACCESS_STACK_SP(0);

                REG_SP -= sizeof(FiberContext);
                stratum::util::MemoryCopy(&fiber->context.context, stack->stack + regs->SP.reg, sizeof(FiberContext));

                code = fiber->context.code;

                // FIXME: Cleanup parameters!

                continue;
            }
            TARGET_OP(CALL) {
                const auto src = FETCH_R_SRC(instr);
                const auto arity = FETCH_IMM(instr);

                const auto func = (Function *) REG_N(src);

                if (!stack->Check(fiber->isolate, regs->SP.reg,
                                  sizeof(FiberContext) + (func->shared->code->stack_size * sizeof(void *)))) {
                    // TODO: ERROR;
                }

                // TODO: Check arity and parameters here

                stratum::util::MemoryCopy(stack->stack + regs->SP.reg, &fiber->context.context, sizeof(FiberContext));
                regs->SP.reg += sizeof(FiberContext);

                fiber->context.context = O_FAST_INCREF(func->shared->context);
                fiber->context.module = O_INCREF(func->shared->module);
                fiber->context.code = O_FAST_INCREF(func->shared->code);

                *ACCESS_STACK_SP(0) = REG_BP;
                regs->SP.reg += sizeof(void *);

                *ACCESS_STACK_SP(0) = REG_IP;
                regs->SP.reg += sizeof(void *);

                REG_BP = REG_SP;

                code = func->shared->code;

                REG_IP = (PtrSize) code->m_code;

                continue;
            }
            TARGET_OP(LDCODE) {
                const auto dst = FETCH_R_DST(instr);
                const auto imm = FETCH_IMM(instr);

                REG_N(dst) = (PtrSize) code->codes->objects[imm];

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
            TARGET_OP(SKLDR) {
                const auto dst = FETCH_R_DST(instr);
                auto slot = ((short) FETCH_IMM(instr)) * (short) sizeof(void *);

                if (slot < 0)
                    slot -= kSkOffset;

                REG_N(dst) = *ACCESS_STACK_BP(slot);

                DISPATCH;
            }
            TARGET_OP(SKSTR) {
                const auto src = FETCH_R_SRC(instr);
                auto slot = ((short) FETCH_IMM(instr)) * (short) sizeof(void *);
                auto value = (OObject *) REG_N(src);

                if (slot < 0)
                    slot -= kSkOffset;

                const auto target = (OObject **) ACCESS_STACK_BP(slot);

                O_DECREF(*target);

                *target = value;

                O_INCREF(value);

                DISPATCH;
            }
            TARGET_OP(PUSH) {
                const auto src = FETCH_R_SRC(instr);
                auto value = (OObject *) REG_N(src);

                if (stack->Check(fiber->isolate, regs->SP.reg, sizeof(void *))) {
                    // TODO: Error!
                }

                *ACCESS_STACK_SP(0) = (PtrSize) value;
                REG_SP += sizeof(void *);

                if (O_IS_OBJECT(value))
                    O_GET_RC(value).IncStrong();

                DISPATCH;
            }
            TARGET_OP(POP) {
                // const auto dst = FETCH_R_DST(instr);
                const auto target = (OObject **) LOAD_FROM_STACK;
                auto value = *target;

                *target = nullptr;

                REG_SP -= sizeof(void *);

                // REG_N(dst) = (PtrSize) value;

                O_DECREF(value);

                DISPATCH;
            }
            TARGET_OP(POPN) {
                const auto target = (OObject **) ACCESS_STACK_SP(0);

                REG_SP -= FETCH_IMM(instr) * sizeof(void *);

                auto cursor = (OObject **) ACCESS_STACK_SP(0);

                while (cursor < target) {
                    O_DECREF(*cursor);

                    cursor++;
                }

                DISPATCH;
            }
            TARGET_OP(ALLOCA) {
                const auto flags = (AllocaFlags) ((instr >> 16) & 0xFu);
                const auto size = FETCH_IMM(instr) * sizeof(void *);

                if (stack->Check(fiber->isolate, regs->SP.reg, size)) {
                    if (flags == AllocaFlags::ZERO_INIT)
                        memory::MemoryZero(stack->stack + regs->SP.reg, size);

                    regs->SP.reg += size;

                    DISPATCH;
                }

                // TODO: ERROR!

                DISPATCH;
            }
            TARGET_OP(LDFUNC) {
                const auto dst = FETCH_R_DST(instr);
                const auto src = FETCH_R_SRC(instr);
                const auto flags = (LoadFuncFlags) ((instr >> 12) & 0x3F);

                auto fn_kind = (FunctionKind) 0;
                Dict *defs = nullptr;

                if (flags == LoadFuncFlags::ASYNC)
                    fn_kind = FunctionKind::ASYNC;

                if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::DEF_ARGS))
                    defs = (Dict *) *LOAD_FROM_STACK;

                auto func = FunctionNew((Code *) REG_N(src), defs, fn_kind);
                if (!func) {
                    // TODO: error!
                }

                REG_N(dst) = (PtrSize) func.get();

                DISPATCH;
            }
            TARGET_OP(NDICT) {
                const auto dst = FETCH_R_DST(instr);
                const auto imm = FETCH_IMM(instr);

                auto dict = DictNew(fiber->isolate);
                if (!dict) {
                    // FIXME: Error!
                }

                REG_N(dst) = (PtrSize) dict.get();

                DISPATCH;
            }
            TARGET_OP(NLIST) {
                const auto dst = FETCH_R_DST(instr);
                const auto imm = FETCH_IMM(instr);

                auto list = ListNew(fiber->isolate, imm);
                if (!list) {
                    // FIXME: Error!
                }

                REG_N(dst) = (PtrSize) list.get();

                DISPATCH;
            }
            TARGET_OP(NSET) {
                const auto dst = FETCH_R_DST(instr);
                const auto imm = FETCH_IMM(instr);

                // TODO: Set here!
                assert(false);

                DISPATCH;
            }
            TARGET_OP(NTUPLE) {
                const auto dst = FETCH_R_DST(instr);
                const auto imm = FETCH_IMM(instr);

                auto tuple = TupleNew(fiber->isolate, imm);
                if (!tuple) {
                    // FIXME: Error!
                }

                REG_N(dst) = (PtrSize) tuple.get();

                DISPATCH;
            }
            TARGET_OP(ADDELEM) {
                const auto dst = FETCH_R_DST(instr);
                const auto src = FETCH_R_SRC(instr);
                const auto value = FETCH_R_RSRC(instr);

                auto *obj = (OObject *) REG_N(dst);

                if (O_IS_TYPE(obj, InstanceType::DICT)) {
                    if (!DictInsert((Dict *) obj, (OObject *) REG_N(src), (OObject *) REG_N(value))) {
                        // FIXME: Error!
                    }
                } else if (O_IS_TYPE(obj, InstanceType::LIST))
                    ListAppend((List *) obj, (OObject *) REG_N(src));
                else if (O_IS_TYPE(obj, InstanceType::TUPLE))
                    TupleAppend((Tuple *) obj, (OObject *) REG_N(src));
                else
                    assert(false);

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
