// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/closure.h>
#include <orbit/orbiter/datatype/ctbuilder.h>
#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/opcode.h>
#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/vm.h>

using namespace orbiter;
using namespace orbiter::datatype;

constexpr auto kStackPrologueOffset = sizeof(FiberContext) + (sizeof(void *) * 2);

// *** Prototypes

void CallNative(Function *func, Registers *regs, const VMStack *stack, U16 argc);

// EOF ***

bool ExecDefer(Fiber *fiber) {
    auto *regs = &fiber->vm.regs;
    const auto *stack = &fiber->vm.stack;

    while (true) {
        auto *defer = fiber->defer_stack.Pop(regs->BP.reg);
        if (defer == nullptr)
            return false;

        auto *func = defer->func;

        if (!defer->func->shared->IsInterpreted()) {
            CallNative(func, regs, stack, defer->argc);

            fiber->isolate->dpool_->DeleteDefer(defer);

            continue;
        }

        fiber->context.context = O_FAST_INCREF(defer->func->shared->context);
        fiber->context.module = O_INCREF(defer->func->shared->module);
        fiber->context.code = O_FAST_INCREF(defer->func->shared->code);
        fiber->context.func = O_FAST_INCREF(defer->func);

        *((PtrSize *) (stack->stack + (defer->SP - sizeof(void *)))) = regs->IP.reg - sizeof(MachineWord);

        regs->r10.reg = defer->r10;
        regs->r11.reg = defer->r11;
        regs->r12.reg = defer->r12;

        regs->BP.reg = defer->SP;

        regs->IP.reg = (PtrSize) func->shared->code->m_code;

        fiber->isolate->dpool_->DeleteDefer(defer);

        return true;
    }
}

bool Call(Fiber *fiber, Function *func, const unsigned short total_args) {
    auto *regs = &fiber->vm.regs;

    if (!func->shared->IsInterpreted()) {
        CallNative(func, regs, &fiber->vm.stack, total_args);

        return false;
    }

    regs->BP.reg = regs->SP.reg;
    regs->IP.reg = (PtrSize) func->shared->code->m_code;

    fiber->context.context = O_FAST_INCREF(func->shared->context);
    fiber->context.module = O_INCREF(func->shared->module);
    fiber->context.code = O_FAST_INCREF(func->shared->code);
    fiber->context.func = O_FAST_INCREF(func);

    return true;
}

bool UnwindStack(Fiber *fiber) {
    auto *regs = &fiber->vm.regs;
    const auto *stack = &fiber->vm.stack;
    const auto *except = (ExceptionContext *) regs->CP.reg;

    while (regs->SP.reg > 0) {
        if (ExecDefer(fiber))
            return true;

        // Stack cleanup
        const auto BP = regs->BP.reg;
        auto SP = regs->SP.reg;
        while (SP != BP) {
            SP -= sizeof(void *);

            O_DECREF(*(OObject**)(stack->stack + SP));
        }

        if (regs->BP.reg == 0)
            break;

        // Load previous frame
        regs->BP.reg -= sizeof(void *);
        regs->IP.reg = (*(PtrSize *) (stack->stack + regs->BP.reg)) + sizeof(MachineWord);

        regs->BP.reg -= sizeof(void *);
        regs->SP.reg = regs->BP.reg;
        regs->BP.reg = *(PtrSize *) (stack->stack + regs->BP.reg);

        SP = regs->SP.reg;
        regs->SP.reg -= sizeof(FiberContext);

        // Is there at least one exception handler?
        if (except != nullptr && except->key == regs->BP.reg) {
            stratum::util::MemoryCopy(&fiber->context.context,
                                      stack->stack + regs->SP.reg,
                                      sizeof(FiberContext));

            // Cleanup parameters
            const auto pops = ((unsigned char *) (regs->CP.reg + sizeof(ExceptionContext))) - stack->stack;
            while (regs->SP.reg > pops) {
                regs->SP.reg -= sizeof(void *);
                O_DECREF(*(OObject**)(stack->stack + regs->SP.reg));
            }

            if (except != nullptr && except->joffset != 0)
                regs->IP.reg = (PtrSize) fiber->context.code->m_code + except->joffset;

            return true;
        }

        regs->IP.reg = (PtrSize) ((FiberContext *) (stack->stack + regs->SP.reg))->code->m_end;

        while (SP > regs->SP.reg) {
            SP -= sizeof(void *);
            O_DECREF(*(OObject**)(stack->stack + SP));
        }
    }

    return false;
}

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

int CallInit(Fiber *fiber, const Function *func, unsigned short p_count, const CallMode mode) {
    auto *regs = &fiber->vm.regs;
    auto *stack = &fiber->vm.stack;

    const auto *fn_shared = func->shared;

    const auto *nargs = (Dict *) regs->r10.reg;
    auto *rest = (List *) regs->r11.reg;
    auto *kwargs = (Dict *) regs->r12.reg;

    const auto arity = fn_shared->arity;

    auto total_args = p_count;

    const bool call_mode_is_kwarg = ENUMBITMASK_ISTRUE(mode, CallMode::KW_ARG);
    const bool call_mode_is_nargs = ENUMBITMASK_ISTRUE(mode, CallMode::NARGS);
    bool call_mode_is_rest = ENUMBITMASK_ISTRUE(mode, CallMode::REST_ARG);

    // *****************************************************************************************************************
    // * Check method information
    // *****************************************************************************************************************

    if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD)) {
        if (func->shared->IsMethod()) {
            // Check object is instance
            const auto args = *((OObject **) ((fiber->vm.stack.stack + fiber->vm.regs.SP.reg)
                                              - (total_args * sizeof(void *))));
            if (!O_GET_HEAD(args).is_instance)
                assert(false); // FIXME: error!
        } else
            total_args -= 1;
    }

    // *****************************************************************************************************************
    // * Check stack size
    // *****************************************************************************************************************

    if (func->currying != nullptr)
        total_args += func->currying->length;

    U32 stack_size_required = 0;

    if (func->shared->IsInterpreted())
        stack_size_required = kStackPrologueOffset
                              + (func->shared->code->stack_size * sizeof(void *))
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
        const auto dict = DictNew(fiber->isolate);
        if (!dict) {
            // TODO: error
            assert(false);
        }

        fiber->vm.Push((OObject *) dict.get());
    }

    if (func->shared->IsInterpreted()) {
        stratum::util::MemoryCopy(stack->stack + regs->SP.reg, &fiber->context.context, sizeof(FiberContext));
        regs->SP.reg += sizeof(FiberContext);

        fiber->vm.Push(regs->BP.reg);
        fiber->vm.Push(regs->IP.reg);
    }

    return total_args;
}

OObject *LoadFromObjectProp(const Fiber *fiber, OObject *obj, const LoadObjectPropFlags flags, const U16 offset) {
    const auto *code = fiber->context.code;
    const auto *type = GetTypeInfoFromObject(obj);

    const auto *key = (ORString *) code->unknown_symbols->objects[offset];
    const PropertyDescriptor *prop = nullptr;

    if (ENUMBITMASK_ISTRUE(flags, LoadObjectPropFlags::SUPER))
        type = O_GET_TYPE(type);

    const TypeInfo *target_type = nullptr;
    prop = TIFindProperty(type, &target_type, (const char *) key->buffer);
    if (prop == nullptr) {
        // FIXME ERROR
        assert(false);
    }

    if (ENUMBITMASK_ISFALSE(prop->detail, PropertyFlag::IS_PUBLIC)) {
        if (fiber->context.func == nullptr
            || fiber->context.func->shared->owner_type == nullptr
            || ENUMBITMASK_ISFALSE(prop->detail, PropertyFlag::IS_PROTECTED)
            || !IsTypeExtends(type, fiber->context.func->shared->owner_type))
            assert(false); // FIXME
    }

    if (ENUMBITMASK_ISTRUE(prop->detail, PropertyFlag::IN_OBJECT)) {
        const auto *slot = O_SLOT(obj, target_type);

        return slot[prop->slot];
    }

    return prop->value;
}

void CallNative(Function *func, Registers *regs, const VMStack *stack, const U16 argc) {
    auto **args = (OObject **) (stack->stack + (regs->SP.reg - (argc * sizeof(void *))));
    const auto old_sp = regs->SP.reg - (argc * sizeof(void *));

    const auto result = func->shared->func(func, args, args[argc], args[argc + 1], argc);

    regs->RR.reg = (PtrSize) result.get();

    // Cleanup native call
    while (regs->SP.reg > old_sp) {
        regs->SP.reg -= sizeof(void *);
        O_DECREF(*(OObject**)(stack->stack + regs->SP.reg));
    }
}

void Return(Fiber *fiber, const U32 pops) {
    auto *regs = &fiber->vm.regs;
    const auto *stack = &fiber->vm.stack;

    // Cleanup local variables
    for (auto i = regs->BP.reg; i != regs->SP.reg; i += sizeof(void *))
        O_DECREF(*(OObject**)(stack->stack + i));

    if (regs->BP.reg > 0) {
        regs->BP.reg -= sizeof(void *);
        regs->IP.reg = *((PtrSize *) (stack->stack + regs->BP.reg)) + sizeof(MachineWord);

        regs->BP.reg -= sizeof(void *);
        regs->SP.reg = regs->BP.reg;

        regs->BP.reg = *((PtrSize *) (stack->stack + regs->SP.reg));

        O_DECREF(fiber->context.context);
        O_DECREF(fiber->context.module);
        O_DECREF(fiber->context.code);
        O_DECREF(fiber->context.func);

        regs->SP.reg -= sizeof(FiberContext);
        stratum::util::MemoryCopy(&fiber->context.context,
                                  stack->stack + regs->SP.reg,
                                  sizeof(FiberContext));

        // Cleanup parameters
        for (auto i = 0; i < pops; i++) {
            regs->SP.reg -= sizeof(void *);
            O_DECREF(*(OObject**)(stack->stack + regs->SP.reg));
        }

        return;
    }

    // Module
    regs->SP.reg = regs->BP.reg;
    regs->IP.reg = (PtrSize) fiber->context.code->m_end;
}

void StoreToObjectProp(const Fiber *fiber, OObject *obj, OObject *value,
                       const LoadObjectPropFlags flags, const U16 offset) {
    const auto *code = fiber->context.code;
    const auto *type = GetTypeInfoFromObject(obj);

    const auto *key = (ORString *) code->unknown_symbols->objects[offset];
    const PropertyDescriptor *prop = nullptr;

    if (ENUMBITMASK_ISTRUE(flags, LoadObjectPropFlags::SUPER))
        type = O_GET_TYPE(type);

    const TypeInfo *target_type = nullptr;
    prop = TIFindProperty(type, &target_type, (const char *) key->buffer);
    if (prop == nullptr) {
        // FIXME ERROR
        assert(false);
    }

    if (ENUMBITMASK_ISFALSE(prop->detail, PropertyFlag::IS_PUBLIC)) {
        if (fiber->context.func == nullptr
            || fiber->context.func->shared->owner_type == nullptr
            || ENUMBITMASK_ISFALSE(prop->detail, PropertyFlag::IS_PROTECTED)
            || !IsTypeExtends(type, fiber->context.func->shared->owner_type))
            assert(false); // FIXME
    }

    if (ENUMBITMASK_ISTRUE(prop->detail, PropertyFlag::IS_CONSTANT))
        assert(false);

    if (ENUMBITMASK_ISTRUE(prop->detail, PropertyFlag::IN_OBJECT)) {
        auto *slot = O_SLOT(obj, target_type);

        slot[prop->slot] = value;
    }
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

BEGIN:
    auto *code = fiber->context.code;
    auto *this_func = fiber->context.func;

    OObject **module_slots = nullptr;
    if (fiber->context.module != nullptr)
        module_slots = O_SLOT(fiber->context.module, O_GET_TYPE(fiber->context.module));

    if (fiber->panic.current_ != nullptr && fiber->panic.current_->frame == REG_BP)
        goto ERROR;

CATCH_FINALLY:
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
            TARGET_OP(PANIC) {
                const auto src = FETCH_R_SRC(instr);
                const auto value = (OObject *) REG_N(src);

                if (!O_IS_OBJECT(value) || !O_IS_TYPE(value, InstanceType::ERROR)) {
                    ErrorSet(fiber->isolate,
                             TypeError::Details[TypeError::Reason::ID],
                             nullptr,
                             TypeError::Details[(int) TypeError::Reason::PANIC],
                             InstanceTypeNames[(int) InstanceType::ERROR]);

                    goto ERROR;
                }

                fiber->Panic(value);

                goto ERROR;
            }
            TARGET_OP(RET) {
                const auto src = FETCH_R_SRC(instr);
                const auto pops = instr & 0xFFFF;

                REG_RR = REG_N(src);

                Return(fiber, pops);

                goto BEGIN;
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

                const auto res = CallInit(fiber, func, p_count, flags);
                if (res < 0)
                    goto ERROR;

                if (Call(fiber, func, res))
                    goto BEGIN;

                DISPATCH;
            }
            TARGET_OP(DEFER) {
                const auto flags = FETCH_F_DST(CallMode, instr);
                const auto src = FETCH_R_SRC(instr);
                const auto p_count = FETCH_IMM(instr);

                const auto func = (Function *) REG_N(src);

                const auto res = CallInit(fiber, func, p_count, flags);
                if (res < 0)
                    goto ERROR;

                auto *defer = fiber->isolate->dpool_->NewDefer();
                if (defer == nullptr)
                    goto ERROR;

                defer->func = O_FAST_INCREF(func);

                defer->argc = res;

                defer->r10 = regs->r10.reg;
                defer->r11 = regs->r11.reg;
                defer->r12 = regs->r12.reg;
                defer->SP = regs->SP.reg;

                fiber->defer_stack.Push(defer, REG_BP);

                DISPATCH;
            }
            TARGET_OP(EXECDEFER) {
                if (ExecDefer(fiber))
                    goto BEGIN;

                DISPATCH;
            }
            TARGET_OP(EXECSUB) {
                const auto src = FETCH_R_SRC(instr);

                const auto sproc = (Code *) REG_N(src);

                if (!stack->Check(fiber->isolate, regs->SP.reg, sproc->stack_size + (3 * sizeof(void *))))
                    goto ERROR;

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

                if (O_IS_OBJECT(value)
                    && O_IS_TYPE(value, InstanceType::FUNCTION)
                    && ((Function *) value)->shared->IsMethod())
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
            TARGET_OP(LDCLO) {
                const auto base = FETCH_R_DST(instr);
                const auto slot = ((short) FETCH_IMM(instr)) * (short) sizeof(void *);
                const auto value = (OObject *) this_func->closure;
                const auto target = (OObject **) (stack->stack + (REG_N(base) + slot));

                *target = O_INCREF(value);

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

                if (!stack->Check(fiber->isolate, regs->SP.reg, sizeof(void *)))
                    goto ERROR;

                *ACCESS_STACK_SP(0) = (PtrSize) value;
                REG_SP += sizeof(void *);

                if (O_IS_OBJECT(value))
                    O_GET_RC(value).IncStrong();

                DISPATCH;
            }
            TARGET_OP(PUSHIF) {
                const auto value = (OObject *) REG_N(FETCH_R_DST(instr));
                const auto target = (OObject *) REG_N(FETCH_R_SRC(instr));
                // const auto against = FETCH_R_RSRC(instr);
                const auto flags = (PushIfFlags) (instr & 0xFu);

                // auto aobj = (OObject *) REG_N(against);

                if (flags == PushIfFlags::METHOD) {
                    if (!O_IS_OBJECT(target) || !O_IS_TYPE(target, InstanceType::FUNCTION)) {
                        // FIXME: Error!
                        assert(false);
                    }

                    if (!((Function *) target)->shared->IsMethod()) {
                        DISPATCH;
                    }
                } else {
                    // TODO: other flags
                    assert(false);
                }

                if (!stack->Check(fiber->isolate, regs->SP.reg, sizeof(void *)))
                    goto ERROR;

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
                auto slot = FETCH_IMM(instr);

                auto *closure = *(Closure **) (stack->stack + regs->BP.reg + (code->vars_count * sizeof(void *)));

                REG_N(dst) = (PtrSize) ClosureGet(closure, slot).get();

                DISPATCH;
            }
            TARGET_OP(CLOSTR) {
                auto src = FETCH_R_DST(instr);
                auto slot = FETCH_IMM(instr);

                auto *closure = *(Closure **) (stack->stack + regs->BP.reg + (code->vars_count * sizeof(void *)));

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

                auto *closure = *(Closure **) (stack->stack + regs->BP.reg + (code->vars_count * sizeof(void *)));
                Tuple *defs = nullptr;

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
                // const auto imm = FETCH_IMM(instr);

                auto dict = DictNew(fiber->isolate);
                if (!dict) {
                    // FIXME: Error!
                }

                REG_N(dst) = (PtrSize) dict.get();

                DISPATCH;
            }
            TARGET_OP(NERROR) {
                const auto dst = FETCH_R_DST(instr);
                const auto kind = (Atom *) REG_N(FETCH_R_SRC(instr));
                const auto reason = (ORString *) REG_N(FETCH_R_RSRC(instr));
                const auto details = (OObject *) REG_N(((instr >> 8) & 0xFu));

                const auto err = ErrorNew(fiber->isolate, kind, reason, details);

                REG_N(dst) = (PtrSize) err.get();

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

                // Fast path
                if (((int) flags & 1) == (int) LoadObjectPropFlags::INLINE) {
                    auto *slot = O_SLOT(src, this_func->shared->owner_type);

                    REG_N(dst) = (PtrSize) slot[offset];

                    DISPATCH;
                }

                REG_N(dst) = (PtrSize) LoadFromObjectProp(fiber, src, flags, offset);

                DISPATCH;
            }
            TARGET_OP(STOBJP) {
                const auto obj = (OObject *) REG_N(FETCH_R_DST(instr));
                const auto value = (OObject *) REG_N(FETCH_R_SRC(instr));
                const auto flags = (LoadObjectPropFlags) FETCH_R_RSRC(instr);
                const auto offset = instr & 0xFFF;

                // Fast path
                if (flags == LoadObjectPropFlags::INLINE) {
                    auto *slot = O_SLOT(obj, this_func->shared->owner_type);

                    slot[offset] = value;

                    DISPATCH;
                }

                StoreToObjectProp(fiber, obj, value, flags, offset);

                DISPATCH;
            }
            TARGET_OP(MKCLZ) {
                const auto dst = FETCH_R_DST(instr);
                const auto flags = FETCH_F_SRC(ClassFlags, instr);
                const auto impls = FETCH_IMM(instr);

                auto clazz = ClassTypeNew(code,
                                          flags == ClassFlags::EXTEND
                                              ? (TypeInfo *) *ACCESS_STACK_SP(-((impls+1) * sizeof(void*)))
                                              : nullptr,
                                          (TypeInfo **) ACCESS_STACK_SP(-(impls * sizeof(void*))),
                                          impls);
                if (!clazz) {
                    // FIXME: Error!
                }

                REG_N(dst) = (PtrSize) clazz.get();

                DISPATCH;
            }
            TARGET_OP(MKTRT) {
                const auto dst = FETCH_R_DST(instr);
                const auto impls = FETCH_IMM(instr);

                auto trait = TraitTypeNew(code, (TypeInfo **) ACCESS_STACK_SP(-(impls * sizeof(void*))), impls);
                if (!trait) {
                    // FIXME: Error!
                }

                REG_N(dst) = (PtrSize) trait.get();

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
            TARGET_OP(JERR) {
                const auto src = FETCH_R_SRC(instr);
                const auto offset = FETCH_IMM(instr);
                auto *e_key = (Atom *) REG_N(src);

                if (((Error *) fiber->panic.current_->error)->kind == e_key) {
                    // Store error in current exception context
                    ((ExceptionContext *) regs->CP.reg)->ret_value = (PtrSize) fiber->GetDiscardPanic().release();

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
                const auto offset = instr & 0xFFFFFFu;

                JMP_TO(offset);

                continue;
            }
            TARGET_OP(TBGIN) {
                const auto offset = instr & 0xFFFFFFu;

                const auto ctx = fiber->vm.e_stack.Push((ExceptionContext *) regs->CP.reg, offset);
                if (ctx == nullptr) {
                    ErrorSet(fiber->isolate,
                             MemoryError::Details[MemoryError::Reason::ID],
                             nullptr,
                             MemoryError::Details[(int) MemoryError::Reason::ESTACK]);

                    goto ERROR;
                }

                ctx->key = regs->BP.reg;

                regs->CP.reg = (PtrSize) ctx;

                DISPATCH;
            }
            TARGET_OP(TEND) {
                const auto ctx = (ExceptionContext *) regs->CP.reg;

                if (fiber->panic.current_ != nullptr) {
                    regs->CP.reg = (PtrSize) ((ExceptionContext *) regs->CP.reg)->prev;

                    fiber->vm.e_stack.Pop();

                    goto ERROR;
                }

                if (ctx->action == (U32) PendingAction::RETURN) {
                    if (ExecDefer(fiber))
                        goto BEGIN;

                    REG_RR = ctx->ret_value;

                    O_DECREF((OObject*)ctx->ret_value);

                    regs->CP.reg = (PtrSize) ((ExceptionContext *) regs->CP.reg)->prev;

                    fiber->vm.e_stack.Pop();

                    Return(fiber, ctx->ret_pops);

                    goto BEGIN;
                }

                O_DECREF((OObject*)ctx->ret_value);

                regs->CP.reg = (PtrSize) ((ExceptionContext *) regs->CP.reg)->prev;

                fiber->vm.e_stack.Pop();

                DISPATCH;
            }
            TARGET_OP(TSPA) {
                const auto action = (PendingAction) ((instr >> 22) & 0x3u);
                const auto src = ((instr >> 18) & 0xF);
                const auto offset = instr & 0x3FFFFu;

                auto ctx = (ExceptionContext *) regs->CP.reg;

                ctx->action = (U32) action;
                ctx->ret_pops = offset;

                if (action == PendingAction::RETURN) {
                    O_DECREF((OObject*)ctx->ret_value);

                    ctx->ret_value = REG_N(src);

                    O_INCREF((OObject*)ctx->ret_value);
                }

                DISPATCH;
            }
            TARGET_OP(LDEXC) {
                const auto dst = FETCH_R_DST(instr);

                REG_N(dst) = ((ExceptionContext *) regs->CP.reg)->ret_value;

                DISPATCH;
            }
            default:
                assert(false);
                DISPATCH;
        }

        NEXT_IP;
    }

ERROR:
    if (fiber->panic.current_ != nullptr) {
        auto *except = (ExceptionContext *) regs->CP.reg;
        if (except != nullptr) {
            if (except->key == regs->BP.reg) {
                // if (except->catch_offset != 0)
                JMP_TO(except->joffset);

                goto CATCH_FINALLY;
            }
        }
    }

    if (UnwindStack(fiber))
        goto BEGIN;

    return (OObject *) regs->RR.reg;
}
