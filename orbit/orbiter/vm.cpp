// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/closure.h>
#include <orbit/orbiter/datatype/ctbuilder.h>
#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/opcode.h>
#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/vm.h>

using namespace orbiter;
using namespace orbiter::datatype;

constexpr auto kStackPrologueOffset = sizeof(FiberContext) + (sizeof(void *) * 2);

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

int VMCall(Fiber *fiber, Function *func, unsigned short p_count, CallMode mode) {
    auto *regs = &fiber->vm.regs;
    auto *stack = &fiber->vm.stack;

    const auto *fn_shared = func->shared;

    auto *nargs = (Dict *) regs->r10.reg;
    auto *rest = (List *) regs->r11.reg;
    auto *kwargs = (Dict *) regs->r12.reg;

    const auto arity = fn_shared->arity;

    auto total_args = p_count;

    const bool call_mode_is_nargs = ENUMBITMASK_ISTRUE(mode, CallMode::NARGS);
    bool call_mode_is_rest = ENUMBITMASK_ISTRUE(mode, CallMode::REST_ARG);
    const bool call_mode_is_kwarg = ENUMBITMASK_ISTRUE(mode, CallMode::KW_ARG);

    if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD) && !func->shared->IsMethod())
        total_args -= 1;

    if (func->currying != nullptr)
        total_args += func->currying->length;

    // *****************************************************************************************************************
    // * Check stack size
    // *****************************************************************************************************************

    auto stack_size_required = kStackPrologueOffset
                               + func->shared->code->stack_size * sizeof(void *)
                               + (arity * sizeof(void *))
                               + (2 * sizeof(void *));

    if (func->shared->HasDefaultArgs())
        stack_size_required += (fn_shared->defaults->length / 2) * sizeof(void *);

    if (!stack->Check(fiber->isolate, regs->SP.reg, stack_size_required)) {
        // TODO: out of memory
    }

    // *****************************************************************************************************************
    // * Check for partial application
    // *****************************************************************************************************************

    if (total_args < arity) {
        const auto args_diff = arity - total_args;

        if (!call_mode_is_rest || rest->length < args_diff) {
            if (func->shared->IsInit()) {
                assert(false); // FIXME
            }

            const auto args = (OObject **) ((fiber->vm.stack.stack + fiber->vm.regs.SP.reg)
                                            - (total_args * sizeof(void *)));

            regs->RR.reg = (PtrSize) FunctionNew(func, args, total_args).get();

            return 2;
        }

        if (!func->shared->IsVariadic() && rest->length > args_diff) {
            // FIXME: GO TO ERROR! TOO many args
        }

        // FIXME: Expand
        for (auto i = 0; i < args_diff; i++) {
            *((OObject **) (fiber->vm.stack.stack + fiber->vm.regs.SP.reg)) = O_INCREF(rest->objects[i]);
            fiber->vm.regs.SP.reg += sizeof(void *);
        }
        // FIXME: EOL

        if (func->shared->IsVariadic()) {
            // TODO: Create a new array containing all the elements from the original one, excluding those already pushed onto the stack!
        }
    }

    auto total_args_with_rest = total_args;
    if (call_mode_is_rest)
        total_args_with_rest += rest->length;

    if (total_args_with_rest > arity) {
        if (!func->shared->IsVariadic()) {
            // TODO: Too much args
            assert(false);
        }

        // TODO: Construct the array and load it into R11. If the call is variadic, merge the new array with the original one and replace it.
        call_mode_is_rest = true;
    }

    // *****************************************************************************************************************
    // * EXPAND NAMED/DEFAULT ARGUMENTS
    // *****************************************************************************************************************

    if (func->shared->HasDefaultArgs()) {
        const auto defaults = func->shared->defaults;

        for (auto i = 0; i < defaults->length; i += 2) {
            HOObject out;

            if (call_mode_is_nargs && DictLookup(nargs, defaults->objects[i], out)) {
                fiber->vm.Push(out.get());

                continue;
            }

            if (call_mode_is_kwarg && DictLookup(kwargs, defaults->objects[i], out)) {
                fiber->vm.Push(out.get());

                continue;
            }

            fiber->vm.Push(defaults->objects[i + 1]);
        }
    } else if (call_mode_is_nargs) {
        // TODO: This function doesn't accept named args
    }

    // *****************************************************************************************************************
    // * Check Rest args
    // *****************************************************************************************************************

    if (call_mode_is_rest)
        fiber->vm.Push((OObject *) rest);
    else if (func->shared->IsVariadic()) {
        auto list = ListNew(fiber->isolate, 0);
        if (!list) {
            // TODO: error
            assert(false);
        }

        fiber->vm.Push((OObject *) list.get());
    }

    // *****************************************************************************************************************
    // * Check KWArgs
    // *****************************************************************************************************************

    if (call_mode_is_kwarg) {
        if (!ENUMBITMASK_ISTRUE(func->shared->kind, FunctionKind::KWARGS)) {
            // TODO: error!
        }

        fiber->vm.Push((OObject *) kwargs);
    } else if (func->shared->IsKWargs()) {
        auto dict = DictNew(fiber->isolate);
        if (!dict) {
            // TODO: error
            assert(false);
        }

        fiber->vm.Push((OObject *) dict.get());
    }

    // *****************************************************************************************************************
    // * SETUP CLOSURE OBJECT (if any)
    // *****************************************************************************************************************

    if (func->closure != nullptr)
        fiber->vm.Push((OObject *) func->closure);

    // *****************************************************************************************************************
    // * ADJUST STACK AND REGISTERS
    // *****************************************************************************************************************

    stratum::util::MemoryCopy(stack->stack + regs->SP.reg, &fiber->context.context, sizeof(FiberContext));
    regs->SP.reg += sizeof(FiberContext);

    fiber->context.context = O_FAST_INCREF(func->shared->context);
    fiber->context.module = O_INCREF(func->shared->module);
    fiber->context.code = O_FAST_INCREF(func->shared->code);
    fiber->context.func = O_FAST_INCREF(func);

    fiber->vm.Push(regs->BP.reg);
    fiber->vm.Push(regs->IP.reg);

    regs->BP.reg = regs->SP.reg;

    regs->IP.reg = (PtrSize) func->shared->code->m_code;

    return 1;
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

#define FETCH_R_DST(instr)          ((instr >> 20) & 0xFu)
#define FETCH_F_DST(target, instr)  ((target) FETCH_R_DST(instr))
#define FETCH_R_SRC(instr)          ((instr >> 16) & 0xFu)
#define FETCH_F_SRC(target, instr)  ((target) FETCH_R_SRC(instr))
#define FETCH_R_RSRC(instr)         ((instr >> 12) & 0xFu)
#define FETCH_IMM(instr)            ((instr) & 0xFFFFu)

#define ACCESS_STACK_BP(offset) ((PtrSize *)(stack->stack + (regs->BP.reg + (offset))))
#define ACCESS_STACK_SP(offset) ((PtrSize *)(stack->stack + (regs->SP.reg + (offset))))
#define LOAD_FROM_STACK         ACCESS_STACK_SP((-sizeof(void *)))

    auto *code = fiber->context.code;
    auto *this_func = fiber->context.func;

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
                const auto pops = instr & 0xFFFF;

                // Cleanup local variables
                for (auto i = regs->BP.reg; i != regs->SP.reg; i += sizeof(void *))
                    O_DECREF(*(OObject**)(stack->stack + i));

                REG_RR = REG_N(src);

                REG_BP -= sizeof(void *);
                REG_IP = *ACCESS_STACK_BP(0) + sizeof(MachineWord);

                REG_BP -= sizeof(void *);
                REG_SP = REG_BP;

                REG_BP = *ACCESS_STACK_SP(0);

                REG_SP -= sizeof(FiberContext);
                stratum::util::MemoryCopy(&fiber->context.context, stack->stack + regs->SP.reg, sizeof(FiberContext));

                // Cleanup parameters
                for (auto i = 0; i < pops; i++) {
                    regs->SP.reg -= sizeof(void *);
                    O_DECREF(*(OObject**)(stack->stack + regs->SP.reg));
                }

                code = fiber->context.code;
                this_func = fiber->context.func;

                continue;
            }
            TARGET_OP(RETSUB) {
                const auto src = FETCH_R_SRC(instr);

                // Cleanup local variables
                for (auto i = regs->BP.reg; i != regs->SP.reg; i += sizeof(void *))
                    O_DECREF(*(OObject**)(stack->stack + i));

                REG_RR = REG_N(src);

                REG_BP -= sizeof(void *);
                REG_IP = *ACCESS_STACK_BP(0) + sizeof(MachineWord);

                REG_BP -= sizeof(void *);
                REG_SP = REG_BP;

                REG_BP = *ACCESS_STACK_SP(0);

                REG_SP -= sizeof(void *);

                O_FAST_DECREF(fiber->context.code);

                fiber->context.code = (Code *) *ACCESS_STACK_SP(0);

                code = fiber->context.code;

                continue;
            }
            TARGET_OP(CALL) {
                const auto flags = FETCH_F_DST(CallMode, instr);
                const auto src = FETCH_R_SRC(instr);
                const auto p_count = FETCH_IMM(instr);

                const auto func = (Function *) REG_N(src);

                const auto res = VMCall(fiber, func, p_count, flags);
                if (res == 1) {
                    code = func->shared->code;
                    this_func = fiber->context.func;

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(EXECSUB) {
                const auto src = FETCH_R_SRC(instr);

                const auto sproc = (Code *) REG_N(src);

                if (!stack->Check(fiber->isolate, regs->SP.reg, sproc->stack_size + (3 * sizeof(void *)))) {
                    // TODO: out of memory
                }

                fiber->vm.Push((OObject *) code);
                fiber->vm.Push(regs->BP.reg);
                fiber->vm.Push(regs->IP.reg);

                regs->BP.reg = regs->SP.reg;

                fiber->context.code = O_FAST_INCREF(sproc);
                code = sproc;

                regs->IP.reg = (PtrSize) sproc->m_code;

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

                REG_N(dst) = shift == 0 ? imm : REG_N(dst) | (imm << (16 * shift));

                DISPATCH;
            }
            TARGET_OP(SETPROP) {
                const auto dst = FETCH_R_DST(instr);
                const auto src = FETCH_R_SRC(instr);
                const auto offset = FETCH_IMM(instr);

                auto *tp = (TypeInfo *) REG_N(dst);
                const auto *key = (ORString *) code->unknown_symbols->objects[offset];
                auto *value = (OObject *) REG_N(src);

                auto prop = TIFindLocalProperty(tp, (const char *) key->buffer);
                if (prop == nullptr) {
                    // FIXME: error
                    assert(false);
                }

                assert(prop->value==nullptr);

                if (O_IS_TYPE(value, InstanceType::FUNCTION) && ((Function *) value)->shared->IsMethod())
                    ((Function *) value)->shared->owner_type = O_FAST_INCREF(tp);

                prop->value = O_INCREF(value);

                DISPATCH;
            }
            TARGET_OP(NGBLV) {
                const auto flags = FETCH_F_DST(VariableFlags, instr);
                const auto value = REG_N(FETCH_R_SRC(instr));
                const auto k_index = FETCH_IMM(instr);
                auto gbl_flags = (PropertyFlag) 0;

                if (ENUMBITMASK_ISTRUE(flags, VariableFlags::CONSTANT))
                    gbl_flags |= PropertyFlag::IS_CONSTANT;

                if (ENUMBITMASK_ISTRUE(flags, VariableFlags::PUBLIC))
                    gbl_flags |= PropertyFlag::IS_PUBLIC;

                if (!ContextDefine(fiber->context.context,
                                   (ORString *) code->unknown_symbols->objects[k_index],
                                   (OObject *) value,
                                   gbl_flags)) {
                    // TODO: Error!
                }

                DISPATCH;
            }
            TARGET_OP(STGBL) {
                const auto value = REG_N(FETCH_R_SRC(instr));
                const auto k_index = FETCH_IMM(instr);

                if (!ContextSet(fiber->context.context,
                                (ORString *) code->unknown_symbols->objects[k_index],
                                (OObject *) value)) {
                    // TODO: Error!
                }

                DISPATCH;
            }
            TARGET_OP(STGOFF) {
                const auto src = FETCH_R_SRC(instr);
                const auto value = REG_N(src);
                const auto slot = FETCH_IMM(instr);

                assert(module_slots != nullptr);

                *(module_slots + slot) = (OObject *) value;

                DISPATCH;
            }
            TARGET_OP(LDGBL) {
                const auto dst = FETCH_R_DST(instr);
                const auto k_index = FETCH_IMM(instr);
                HOObject out;

                if (!ContextLookup(fiber->context.context, (ORString *) code->unknown_symbols->objects[k_index],
                                   out, nullptr)) {
                    // TODO: Error!
                }

                REG_N(dst) = (PtrSize) out.get();

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
                const auto base = FETCH_R_SRC(instr);
                const auto dst = FETCH_R_DST(instr);
                auto slot = ((short) FETCH_IMM(instr)) * (short) sizeof(void *);

                REG_N(dst) = *((PtrSize *) (stack->stack + (REG_N(base) + slot)));

                DISPATCH;
            }
            TARGET_OP(SKSTR) {
                const auto base = FETCH_R_DST(instr);
                const auto src = FETCH_R_SRC(instr);
                auto slot = ((short) FETCH_IMM(instr)) * (short) sizeof(void *);
                auto value = (OObject *) REG_N(src);

                const auto target = (OObject **) (stack->stack + (REG_N(base) + slot));

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
                const auto dst = FETCH_R_DST(instr);
                const auto target = (OObject **) LOAD_FROM_STACK;
                auto value = *target;

                *target = nullptr;

                REG_SP -= sizeof(void *);

                REG_N(dst) = (PtrSize) value;

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
            TARGET_OP(CLONEW) {
                const auto dst = FETCH_R_DST(instr);
                const auto slots = instr & 0xFFFF;

                auto closure = ClosureNew(fiber->isolate, slots);
                if (!closure) {
                    // FIXME: Error!
                }

                REG_N(dst) = (PtrSize) closure.get();

                DISPATCH;
            }
            TARGET_OP(CLOLDR) {
                auto dst = FETCH_R_DST(instr);
                auto flags = FETCH_F_SRC(ClosureLSMode, instr);
                auto slot = FETCH_IMM(instr);

                Closure *closure = nullptr;

                if (flags == ClosureLSMode::LOCALS_SLOT)
                    closure = *(Closure **) (stack->stack + regs->BP.reg + (code->slots_count * sizeof(void *)));
                else
                    closure = *(Closure **) (stack->stack + (regs->BP.reg - (kStackPrologueOffset + sizeof(void *))));

                REG_N(dst) = (PtrSize) ClosureGet(closure, slot).get();

                DISPATCH;
            }
            TARGET_OP(CLOSTR) {
                auto src = FETCH_R_DST(instr);
                auto flags = FETCH_F_SRC(ClosureLSMode, instr);
                auto slot = FETCH_IMM(instr);

                Closure *closure = nullptr;

                if (flags == ClosureLSMode::LOCALS_SLOT)
                    closure = *(Closure **) (stack->stack + regs->BP.reg + (code->slots_count * sizeof(void *)));
                else
                    closure = *(Closure **) (stack->stack + (regs->BP.reg - (kStackPrologueOffset + sizeof(void *))));

                ClosureSet(closure, slot, (OObject *) REG_N(src));

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
                const auto flags = (LoadFuncFlags) (instr & 0xFFF);

                Closure *closure = nullptr;
                Tuple *defs = nullptr;

                if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::A_CLOSURE))
                    closure = *(Closure **) (stack->stack + regs->BP.reg + (code->slots_count * sizeof(void *)));
                else if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::P_CLOSURE))
                    closure = *(Closure **) (stack->stack + (regs->BP.reg - (kStackPrologueOffset + sizeof(void *))));

                if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::NPARAMS))
                    defs = (Tuple *) REG_N(FETCH_R_RSRC(instr));

                auto func = FunctionNew((Code *) REG_N(src), closure, defs, flags);
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
            TARGET_OP(LDINIT) {
                const auto dst = FETCH_R_DST(instr);
                const auto src = FETCH_R_SRC(instr);

                const auto *tp = (TypeInfo *) REG_N(src);
                const auto *init = TIFindLocalProperty(tp, "init");
                if (init == nullptr) {
                    // FIXME: error
                    assert(false);
                }

                REG_N(dst) = (PtrSize) init->value;

                DISPATCH;
            }
            TARGET_OP(LDOBJP) {
                const auto dst = FETCH_R_DST(instr);
                const auto src = (OObject *) REG_N(FETCH_R_SRC(instr));
                const auto flags = (LoadObjectPropFlags) FETCH_R_RSRC(instr);
                const auto offset = instr & 0xFFF;

                if (((int) flags & 1) == (int) LoadObjectPropFlags::INLINE) {
                    auto *slot = O_SLOT(src, this_func->shared->owner_type);

                    REG_N(dst) = (PtrSize) slot[offset];

                    DISPATCH;
                }

                const auto *key = (ORString *) code->unknown_symbols->objects[offset];
                PropertyDescriptor *prop = nullptr;

                if (ENUMBITMASK_ISTRUE(flags, LoadObjectPropFlags::SUPER))
                    prop = TIFindProperty(O_GET_TYPE(O_GET_TYPE(src)), (const char *) key->buffer);
                else
                    prop = TIFindProperty(O_GET_TYPE(src), (const char *) key->buffer);
                if (prop == nullptr) {
                    // FIXME ERROR
                    assert(false);
                }

                // FIXME: Check security permission

                if (ENUMBITMASK_ISTRUE(prop->detail, PropertyFlag::IN_OBJECT)) {
                    auto *slot = O_SLOT(src, this_func->shared->owner_type);

                    REG_N(dst) = (PtrSize) slot[(PtrSize) prop->value];
                } else
                    REG_N(dst) = (PtrSize) prop->value;


                DISPATCH;
            }
            TARGET_OP(STOBJP) {
                const auto obj = (OObject *) REG_N(FETCH_R_DST(instr));
                const auto value = (OObject *) REG_N(FETCH_R_SRC(instr));
                const auto flags = (LoadObjectPropFlags) FETCH_R_RSRC(instr);
                const auto offset = instr & 0xFFF;

                if (flags == LoadObjectPropFlags::INLINE) {
                    auto *slot = O_SLOT(obj, this_func->shared->owner_type);

                    slot[offset] = value;

                    DISPATCH;
                }

                assert(false);

                DISPATCH;
            }
            TARGET_OP(MKCLZ) {
                const auto dst = FETCH_R_DST(instr);
                const auto flags = FETCH_F_SRC(ClassFlags, instr);
                const auto impls = FETCH_IMM(instr);

                auto sk_start = impls;

                auto clazz = ClassTypeNew(code,
                                          flags == ClassFlags::EXTEND
                                              ? (TypeInfo *) *ACCESS_STACK_SP(-((sk_start+1) * sizeof(void*)))
                                              : nullptr,
                                          (TypeInfo **) ACCESS_STACK_SP(-(sk_start * sizeof(void*))),
                                          sk_start);
                if (!clazz) {
                    // FIXME: Error!
                }

                REG_N(dst) = (PtrSize) clazz.get();

                DISPATCH;
            }
            TARGET_OP(NOBJ) {
                const auto dst = FETCH_R_DST(instr);
                const auto src = FETCH_R_SRC(instr);

                auto *tp = (TypeInfo *) REG_N(src);

                REG_N(dst) = (PtrSize) ClassNew(tp).get();

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
                assert(false);
                DISPATCH;
        }

        NEXT_IP;
    }

    return (OObject *) regs->RR.reg;
}
