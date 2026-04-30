// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/datatype/chan.h>
#include <orbit/orbiter/datatype/closure.h>
#include <orbit/orbiter/datatype/ctbuilder.h>
#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/future.h>
#include <orbit/orbiter/datatype/generator.h>
#include <orbit/orbiter/datatype/nativefunc.h>
#include <orbit/orbiter/datatype/result.h>
#include <orbit/orbiter/datatype/tuple.h>

#include <orbit/orbiter/native/ffi.h>
#include <orbit/orbiter/native/loader.h>

#include <orbit/orbiter/opcode.h>
#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/excstack.h>
#include <orbit/orbiter/vm.h>

using namespace orbiter;
using namespace orbiter::datatype;

// *** Prototypes

// External

bool ObjectAdd(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectSub(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectMul(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectDiv(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectIDiv(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectMod(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectModR(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectAnd(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectOr(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectXor(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectLShift(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectRShift(Isolate *isolate, const OObject *left, const OObject *right, OObject *&result) noexcept;

bool ObjectNeg(Isolate *isolate, const OObject *object, OObject *&result) noexcept;

bool ObjectMoveNot(Isolate *isolate, const OObject *object, OObject *&result) noexcept;

bool ObjectContains(Isolate *isolate, const OObject *container, const OObject *value, bool &out) noexcept;

// Internal

void CallNative(Function *func, Registers *regs, const VMStack *stack, U16 argc);

void ExecuteCleanupForPC(const Fiber *fiber);

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

        // Store current context
        regs->IP.reg -= sizeof(MachineWord); // Must re-execute the same opcode; PushState saves the next one
        if (!fiber->PushState())
            return false;

        fiber->SetContext(func);

        regs->r10.reg = defer->r10;
        regs->r11.reg = defer->r11;
        regs->r12.reg = defer->r12;

        regs->BP.reg = defer->SP + kStackPrologueOffset;

        regs->IP.reg = (PtrSize) func->shared->code->m_code;

        fiber->isolate->dpool_->DeleteDefer(defer);

        return true;
    }
}

bool Call(Fiber *fiber, Function *func, const U16 total_args) {
    auto *regs = &fiber->vm.regs;

    if (!func->shared->IsInterpreted()) {
        CallNative(func, regs, &fiber->vm.stack, total_args);

        return false;
    }

    if (func->shared->IsAsync()) {
        const auto size = total_args * sizeof(void *);

        regs->RR.reg = (PtrSize) Orbiter::GetInstance()->EvalAsync(func,
                                                                   (fiber->vm.stack.stack + regs->SP.reg) - size,
                                                                   size).get();

        regs->SP.reg -= size;

        return false;
    }

    if (!fiber->PushState())
        return false;

    // Load new context
    fiber->SetContext(func);

    regs->BP.reg = regs->SP.reg;

    return true;
}

bool LoadFromIndex(const Fiber *fiber, OObject *object, const OObject *index, PtrSize &dst) {
    if (O_IS_OBJECT(object)) {
        const auto &ops = O_GET_TYPE_OPS(object);
        if (ops.load_index != nullptr)
            return ops.load_index(object, index, (OObject *&) dst);
    }

    ErrorSetWithObjType(fiber->isolate,
                        TypeError::Details[TypeError::Reason::ID],
                        TypeError::Details[TypeError::Reason::NON_SUBSCRIPTABLE],
                        nullptr,
                        object);

    return false;
}

bool LoadFromSlice(const Fiber *fiber, OObject *object, const OObject *start, const OObject *stop, const OObject *step,
                   PtrSize &dst) {
    if (O_IS_OBJECT(object)) {
        const auto &ops = O_GET_TYPE_OPS(object);
        if (ops.load_slice != nullptr)
            return ops.load_slice(object, start, stop, step, (OObject *&) dst);
    }

    ErrorSetWithObjType(fiber->isolate,
                        TypeError::Details[TypeError::Reason::ID],
                        TypeError::Details[TypeError::Reason::NON_SUBSCRIPTABLE],
                        nullptr,
                        object);

    return false;
}

bool StoreToIndex(const Fiber *fiber, OObject *object, const OObject *index, OObject *value) {
    if (O_IS_OBJECT(object)) {
        const auto &ops = O_GET_TYPE_OPS(object);
        if (ops.store_index != nullptr)
            return ops.store_index(object, index, value);
    }

    ErrorSetWithObjType(fiber->isolate,
                        TypeError::Details[TypeError::Reason::ID],
                        TypeError::Details[TypeError::Reason::NON_SUBSCRIPTABLE],
                        nullptr,
                        object);

    return false;
}

bool UnwindStack(Fiber *fiber) {
    const auto *context = &fiber->context;
    auto *regs = &fiber->vm.regs;
    const auto *stack = &fiber->vm.stack;
    const auto *except = (ExceptionContext *) regs->CP.reg;

    while (regs->SP.reg > 0) {
        ExecuteCleanupForPC(fiber);

        if (ExecDefer(fiber))
            return true;

        regs->SP.reg = regs->BP.reg;

        if (regs->BP.reg == 0)
            break;

        // Load previous frame
        regs->BP.reg -= sizeof(void *);

        regs->IP.reg = *(PtrSize *) (stack->stack + regs->BP.reg);
        regs->IP.reg &= ~0x01;

        regs->BP.reg -= sizeof(void *);
        regs->SP.reg = regs->BP.reg;

        regs->BP.reg = *(PtrSize *) (stack->stack + regs->BP.reg);
        regs->BP.reg &= ~0x01;

        regs->SP.reg -= sizeof(FiberContext);

        if (context == &fiber->context) {
            if (context->func != nullptr && O_IS_TYPE(context->func, InstanceType::GENERATOR)) {
                ((Generator *) context->func)->state = GeneratorState::EXHAUSTED;
                ((Generator *) context->func)->acquired = 0;
            }
        }

        context = (FiberContext *) (stack->stack + regs->SP.reg);

        // Is there at least one exception handler?
        if (except != nullptr && except->key == regs->BP.reg) {
            stratum::util::MemoryCopy(&fiber->context.context, context, sizeof(FiberContext));

            // The stack pointer must be adjusted to point to the end of the exception block on the stack.
            regs->SP.reg = ((unsigned char *) (regs->CP.reg + sizeof(ExceptionContext))) - stack->stack;

            if (except->coffset != 0)
                regs->IP.reg = (PtrSize) fiber->context.code->m_code + except->coffset;

            return true;
        }

        if (context->func != nullptr && O_IS_TYPE(context->func, InstanceType::GENERATOR)) {
            ((Generator *) context->func)->state = GeneratorState::EXHAUSTED;
            ((Generator *) context->func)->acquired = 0;
        }

        // This can only occur if the code is executing in a spawned fiber
        if (context->code == nullptr)
            return false;

        regs->IP.reg = (PtrSize) context->code->m_end;
    }

    return false;
}

bool VMGetIter(const Fiber *fiber, OObject *object, PtrSize *dst) {
    if (O_IS_OBJECT(object)) {
        const auto *type = (TypeInfoOps *) O_GET_TYPE(object);

        // Generator fast path
        if (type->type.i_type == InstanceType::GENERATOR) {
            *dst = (PtrSize) object;

            return true;
        }

        if (type->ops.get_iter != nullptr) {
            *dst = (PtrSize) type->ops.get_iter(object);

            return true;
        }
    }

    ErrorSetWithObjType(fiber->isolate,
                        TypeError::Details[TypeError::Reason::ID],
                        TypeError::Details[TypeError::Reason::NON_ITERABLE],
                        nullptr,
                        object);

    return false;
}

// *********************************************************************************************************************
// VM CALL
// *********************************************************************************************************************

struct CallCtx {
    Registers *regs;
    VMStack *stack;

    const FuncShared *fn_shared;

    Dict *nargs;
    List *rest;
    Dict *kwargs;

    U16 stack_args;

    bool call_mode_is_kwarg;
    bool call_mode_is_nargs;
    bool call_mode_is_rest;
};

bool CallNormalizeMethodCall(Isolate *isolate, CallCtx &ctx) {
    if (!ctx.fn_shared->IsMethod()) {
        ctx.stack_args -= 1;
        return true;
    }

    // Check object is instance
    const auto args = *((OObject **) ((ctx.stack->stack + ctx.regs->SP.reg) - (ctx.stack_args * sizeof(void *))));
    if (!O_GET_RC(args).IsInstance()) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::METHOD_RECEIVER],
                            nullptr,
                            args);

        return false;
    }

    return true;
}

bool CallEnsureStack(Isolate *isolate, const CallCtx &ctx) {
    U32 stack_size_required = 0;

    if (ctx.fn_shared->IsInterpreted())
        stack_size_required = kStackPrologueOffset
                              + (ctx.fn_shared->code->stack_size * sizeof(void *))
                              + (2 * sizeof(void *));

    if (ctx.fn_shared->HasDefaultArgs())
        stack_size_required += (ctx.fn_shared->defaults->length / 2) * sizeof(void *);

    return ctx.stack->Check(isolate, ctx.regs->SP.reg, stack_size_required);
}

bool CallExpandDefaultArgs(Fiber *fiber, CallCtx &ctx) {
    if (!ctx.fn_shared->HasDefaultArgs()) {
        if (ctx.call_mode_is_nargs) {
            ErrorSet(fiber->isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::NO_NAMED_ARGS],
                     ORSTRING_TO_CSTR(ctx.fn_shared->name));

            return false;
        }

        return true;
    }

    const auto defaults = ctx.fn_shared->defaults;
    for (auto i = 0; i < defaults->length; i += 2) {
        HOObject out;

        ctx.stack_args += 1;

        if (ctx.call_mode_is_nargs && DictLookup(ctx.nargs, defaults->objects[i], out) == LookupResult::OK) {
            fiber->vm.Push(out.get());

            continue;
        }

        if (ctx.call_mode_is_kwarg && DictLookup(ctx.kwargs, defaults->objects[i], out) == LookupResult::OK) {
            fiber->vm.Push(out.get());

            continue;
        }

        // Push default
        fiber->vm.Push(defaults->objects[i + 1]);
    }

    return true;
}

bool CallFinalizeKwargs(Fiber *fiber, const CallCtx &ctx) {
    if (ctx.call_mode_is_kwarg) {
        if (ENUMBITMASK_ISFALSE(ctx.fn_shared->kind, FunctionKind::KWARGS)) {
            ErrorSet(fiber->isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::NO_KWARGS],
                     ORSTRING_TO_CSTR(ctx.fn_shared->name));

            return false;
        }

        fiber->vm.Push((OObject *) ctx.kwargs);

        return true;
    }

    if (ctx.fn_shared->IsKWargs()) {
        const auto dict = DictNew(fiber->isolate);
        if (!dict)
            return false;

        fiber->vm.Push((OObject *) dict.get());
    }

    return true;
}

bool CallFinalizeRestArgs(Fiber *fiber, const CallCtx &ctx) {
    if (ctx.call_mode_is_rest) {
        fiber->vm.Push((OObject *) ctx.rest);

        return true;
    }

    if (ctx.fn_shared->IsVariadic()) {
        const auto list = ListNew(fiber->isolate, 0);
        if (!list)
            return false;

        fiber->vm.Push((OObject *) list.get());
    }

    return true;
}

void CallLoadCurrying(const Function *func, CallCtx &ctx) {
    const auto args = (OObject **) ((ctx.stack->stack + ctx.regs->SP.reg) - (ctx.stack_args * sizeof(void *)));
    const auto offset = func->currying->length;

    for (auto i = 0; i < ctx.stack_args; i++)
        args[offset + i] = args[i];

    for (auto i = 0; i < func->currying->length; i++)
        args[i] = func->currying->objects[i];

    ctx.stack_args += func->currying->length;
    ctx.regs->SP.reg += (offset * sizeof(void *));
}

int CallInit(Fiber *fiber, Function *&func, const unsigned short p_count, const CallMode mode) {
    if (!O_IS_OBJECT(func)) {
        ErrorSetWithObjType(fiber->isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::NON_CALLABLE],
                            nullptr,
                            (OObject *) func);

        return (int) CallResult::ERROR;
    }

    if (!O_IS_TYPE(func, InstanceType::FUNCTION)) {
        if (!O_IS_TYPE(func, InstanceType::TYPE) || ((TypeInfo *) func)->ctor == nullptr) {
            ErrorSetWithObjType(fiber->isolate,
                                TypeError::Details[TypeError::Reason::ID],
                                TypeError::Details[TypeError::Reason::NON_CALLABLE],
                                nullptr,
                                (OObject *) func);

            return (int) CallResult::ERROR;
        }

        func = (Function *) ((TypeInfo *) func)->ctor;
    }

    CallCtx ctx{
        .regs = &fiber->vm.regs,
        .stack = &fiber->vm.stack,
        .fn_shared = func->shared,
        .nargs = (Dict *) fiber->vm.regs.r10.reg,
        .rest = (List *) fiber->vm.regs.r11.reg,
        .kwargs = (Dict *) fiber->vm.regs.r12.reg,
        .stack_args = p_count,
        .call_mode_is_kwarg = ENUMBITMASK_ISTRUE(mode, CallMode::KW_ARG),
        .call_mode_is_nargs = ENUMBITMASK_ISTRUE(mode, CallMode::NARGS),
        .call_mode_is_rest = ENUMBITMASK_ISTRUE(mode, CallMode::REST_ARG)
    };

    if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD) && !CallNormalizeMethodCall(fiber->isolate, ctx))
        return (int) CallResult::ERROR;

    if (!CallEnsureStack(fiber->isolate, ctx)) {
        ErrorSet(fiber->isolate,
                 MemoryError::Details[MemoryError::Reason::ID],
                 nullptr,
                 MemoryError::Details[MemoryError::Reason::STACK]);

        return (int) CallResult::ERROR;
    }

    bool rest_edited = false;
    bool currying_pushed = false;

    auto total_args = ctx.stack_args;
    if (func->currying != nullptr)
        total_args += func->currying->length;

    if (total_args < ctx.fn_shared->arity) {
        const auto args_diff = ctx.fn_shared->arity - total_args;

        if (!ctx.call_mode_is_rest || ctx.rest->length < args_diff) {
            if (func->shared->IsInit()) {
                ErrorSet(fiber->isolate,
                         TypeError::Details[TypeError::Reason::ID],
                         nullptr,
                         TypeError::Details[TypeError::Reason::INIT_NO_CURRY],
                         ctx.fn_shared->owner_type->name);

                return (int) CallResult::ERROR;
            }

            const auto args = (OObject **) ((ctx.stack->stack + ctx.regs->SP.reg) - (ctx.stack_args * sizeof(void *)));

            ctx.regs->RR.reg = (PtrSize) FunctionNew(func, args, ctx.stack_args).get();
            ctx.regs->SP.reg -= ctx.stack_args * sizeof(void *);

            return (int) CallResult::DONE;
        }

        if (!func->shared->IsVariadic() && ctx.rest->length > args_diff) {
            ErrorSet(fiber->isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::TOO_MANY_ARGS],
                     ORSTRING_TO_CSTR(ctx.fn_shared->name),
                     (int) ctx.fn_shared->arity,
                     (int) (ctx.stack_args + ctx.rest->length));

            return (int) CallResult::ERROR;
        }

        if (func->currying != nullptr) {
            CallLoadCurrying(func, ctx);

            currying_pushed = true;
        }

        for (auto i = 0; i < args_diff; i++) {
            fiber->vm.Push(ctx.rest->objects[i]);
            ctx.stack_args += 1;
        }

        if (func->shared->IsVariadic()) {
            const auto tmp = ListNew(fiber->isolate, ctx.rest->length - args_diff);
            if (!tmp)
                return (int) CallResult::ERROR;

            ListExtend(tmp.get(), ctx.rest->objects + args_diff, ctx.rest->length - args_diff);

            ctx.rest = tmp.get();

            rest_edited = true;
        }
    }

    if (!currying_pushed && func->currying != nullptr)
        CallLoadCurrying(func, ctx);

    if (ctx.call_mode_is_rest)
        total_args += ctx.rest->length;

    if (total_args > ctx.fn_shared->arity) {
        if (!func->shared->IsVariadic()) {
            ErrorSet(fiber->isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::TOO_MANY_ARGS],
                     ORSTRING_TO_CSTR(ctx.fn_shared->name),
                     (int) ctx.fn_shared->arity,
                     (int) total_args);

            return (int) CallResult::ERROR;
        }

        if (!rest_edited) {
            const auto excess_on_stack = ctx.stack_args > ctx.fn_shared->arity
                                             ? ctx.stack_args - ctx.fn_shared->arity
                                             : 0;

            const auto rest_len = ctx.call_mode_is_rest ? ctx.rest->length : 0;

            const auto tmp = ListNew(fiber->isolate, excess_on_stack + rest_len);
            if (!tmp)
                return (int) CallResult::ERROR;

            if (excess_on_stack > 0) {
                const auto args = (OObject **) ((ctx.stack->stack + ctx.regs->SP.reg)
                                                - (excess_on_stack * sizeof(void *)));
                ListExtend(tmp.get(), args, excess_on_stack);

                ctx.regs->SP.reg -= excess_on_stack * sizeof(void *);
                ctx.stack_args -= excess_on_stack;
            }

            if (ctx.call_mode_is_rest)
                ListExtend(tmp.get(), (OObject *) ctx.rest);

            ctx.rest = tmp.get();
        }

        ctx.call_mode_is_rest = true;
    }

    if (!CallExpandDefaultArgs(fiber, ctx))
        return (int) CallResult::ERROR;

    if (!CallFinalizeRestArgs(fiber, ctx))
        return (int) CallResult::ERROR;

    if (!CallFinalizeKwargs(fiber, ctx))
        return (int) CallResult::ERROR;

    return ctx.stack_args;
}

// *********************************************************************************************************************
// EOF
// *********************************************************************************************************************

int CallGenerator(Fiber *fiber, Generator *gen, const U16 total_args, const CallMode mode, const bool exhausted_error) {
    auto *regs = &fiber->vm.regs;
    const auto *stack = &fiber->vm.stack;

    if (total_args != 0 || mode != CallMode::FASTCALL) {
        ErrorSet(fiber->isolate,
                 TypeError::Details[TypeError::Reason::ID],
                 nullptr,
                 TypeError::Details[TypeError::Reason::GENERATOR_INVALID_CALL]);

        return (int) CallResult::ERROR;
    }

    PtrSize actual = 0;
    if (!gen->acquired.compare_exchange_strong(actual, (PtrSize) fiber))
        return (int) CallResult::BUSY;

    if (gen->state == GeneratorState::EXHAUSTED) {
        gen->acquired = 0;

        if (!exhausted_error)
            return (int) CallResult::EXHAUST;

        ErrorSet(fiber->isolate,
                 StopIterationError::Details[StopIterationError::Reason::ID],
                 nullptr,
                 StopIterationError::Details[StopIterationError::Reason::GENERATOR_EXHAUSTED]);

        return (int) CallResult::ERROR;
    }

    // Load registers
    stratum::util::MemoryCopy(regs, gen->regs_dump, kGeneralPurposeRegistersCount);

    // Load generator params
    const auto params_length = gen->stack - gen->params;
    stratum::util::MemoryCopy(stack->stack + regs->SP.reg, gen->params, params_length);
    regs->SP.reg += params_length;

    // Store current context
    stratum::util::MemoryCopy(stack->stack + regs->SP.reg, &fiber->context.context, sizeof(FiberContext));
    regs->SP.reg += sizeof(FiberContext);

    fiber->vm.Push(regs->BP.reg); // Store BP
    fiber->vm.Push(regs->IP.reg + sizeof(MachineWord)); // Store IP

    const auto BP = regs->SP.reg;

    // Load generator stack
    if (gen->stack_size > 0) {
        stratum::util::MemoryCopy(stack->stack + regs->SP.reg, gen->params, gen->stack_size);
        regs->SP.reg += gen->stack_size;
    }

    // Load context into fiber
    const auto func = gen->base;
    fiber->context.context = func->shared->context;
    fiber->context.module = func->shared->module;
    fiber->context.code = func->shared->code;
    fiber->context.func = (OObject *) gen;

    regs->BP.reg = BP;
    regs->IP.reg = gen->IP;

    gen->state = GeneratorState::RUNNING;

    return (int) CallResult::CONTINUE;
}

int VMGetIterNext(Fiber *fiber, OObject *object, PtrSize *dst) {
    if (O_IS_OBJECT(object)) {
        if (O_IS_TYPE(object, InstanceType::GENERATOR))
            return CallGenerator(fiber, (Generator *) object, 0, CallMode::FASTCALL, false);

        const auto &ops = O_GET_TYPE_OPS(object);
        if (ops.iter_next != nullptr)
            return (int) ops.iter_next(object, (OObject **) dst);
    }

    ErrorSetWithObjType(fiber->isolate,
                        RuntimeError::Details[RuntimeError::Reason::ID],
                        RuntimeError::Details[RuntimeError::Reason::ITER_NEXT_NOT_IMPLEMENTED],
                        nullptr,
                        object);

    return (int) CallResult::ERROR;
}

OObject *LoadFromObjectProp(const Fiber *fiber, const Function *func, OObject *obj, const LoadObjectPropFlags flags,
                            const U16 offset) {
    const auto *code = fiber->context.code;
    const auto *type = GetTypeInfoFromObject(obj);

    const auto *key = (ORString *) code->unknown_symbols->objects[offset];
    const PropertyDescriptor *prop = nullptr;

    if (ENUMBITMASK_ISTRUE(flags, LoadObjectPropFlags::SUPER))
        type = O_GET_TYPE(type);

    const TypeInfo *target_type = nullptr;
    prop = TIFindProperty(type, &target_type, (const char *) key->buffer);
    if (prop == nullptr) {
        char error[24];

        GetTypeName(fiber->isolate, obj, error, sizeof(error));

        ErrorSet(fiber->isolate,
                 AttributeError::Details[AttributeError::Reason::ID],
                 nullptr,
                 AttributeError::Details[AttributeError::Reason::NOT_FOUND],
                 error,
                 (const char *) key->buffer);

        return nullptr;
    }

    if (ENUMBITMASK_ISFALSE(prop->detail, PropertyFlag::IS_PUBLIC)) {
        if (func == nullptr
            || func->shared->owner_type == nullptr
            || (ENUMBITMASK_ISFALSE(prop->detail, PropertyFlag::IS_PROTECTED)
                && !IsTypeExtends(type, func->shared->owner_type))) {
            ErrorSetWithObjType(fiber->isolate,
                                AttributeError::Details[AttributeError::Reason::ID],
                                AttributeError::Details[AttributeError::Reason::PRIVATE_ACCESS],
                                (const char *) key->buffer, obj);

            return nullptr;
        }
    }

    if (ENUMBITMASK_ISTRUE(prop->detail, PropertyFlag::IN_OBJECT)) {
        const auto *slot = O_SLOT(obj, target_type);

        return slot[prop->slot];
    }

    return prop->value;
}

void CallNative(Function *func, Registers *regs, const VMStack *stack, const U16 argc) {
    auto **args = (OObject **) (stack->stack + (regs->SP.reg - (argc * sizeof(void *))));

    const auto result = func->shared->func(func, args, args[argc], args[argc + 1], argc);

    regs->RR.reg = (PtrSize) result.get();
    regs->SP.reg -= (argc * sizeof(void *));
}

void ExecuteCleanupForPC(const Fiber *fiber) {
    const auto count = fiber->context.code->cleanup.length;
    if (count == 0)
        return;

    const auto ip = fiber->vm.regs.IP.reg;

    const auto *table = fiber->context.code->cleanup.entries;

    for (auto i = count - 1; i >= 0; i--) {
        const auto *entry = table + i;

        if (ip < (PtrSize) entry->m_start || ip >= (PtrSize) entry->m_end)
            continue;

        auto *value = *((OObject **) (fiber->vm.stack.stack + (
                                          fiber->vm.regs.BP.reg + (entry->slot * sizeof(void *)))));

        switch (entry->type) {
            case OPCode::SYNC_EXIT:
                MonitorRelease(value);
                break;
            default:
                assert(false);
                break;
        }
    }
}

void ReleaseExceptionContext(Registers *regs) {
    auto *ctx = (ExceptionContext *) regs->CP.reg;

    assert(ctx!=nullptr);

    regs->CP.reg = (PtrSize) ctx->prev;

    memory::MemoryZero(ctx, sizeof(ExceptionContext));
}

void Return(Fiber *fiber, const U32 pops) {
    auto *regs = &fiber->vm.regs;
    auto *func = fiber->context.func;

    if (regs->BP.reg > 0)
        ExecuteCleanupForPC(fiber);

    if (regs->BP.reg > 0) {
        fiber->PopState();

        // Cleanup parameters
        regs->SP.reg -= pops * sizeof(void *);

        if (O_IS_TYPE(func, InstanceType::GENERATOR)) {
            ((Generator *) func)->state = GeneratorState::EXHAUSTED;
            ((Generator *) func)->acquired = 0;
        }

        return;
    }

    // Module
    regs->SP.reg = regs->BP.reg;
    regs->IP.reg = (PtrSize) fiber->context.code->m_end;
}

void SaveGenerator(Fiber *fiber) {
    auto *regs = &fiber->vm.regs;
    const auto *stack = &fiber->vm.stack;

    auto *gen = (Generator *) fiber->context.func;

    // Store stack
    gen->stack_size = regs->SP.reg - regs->BP.reg;
    stratum::util::MemoryCopy(gen->stack, stack->stack + regs->BP.reg, gen->stack_size);

    regs->SP.reg = regs->BP.reg - sizeof(void *);

    // Save the IP and advance it to the next instruction
    gen->IP = regs->IP.reg + sizeof(MachineWord);

    // Restore the caller's IP (saved before entering the generator) and advance to the next instruction
    regs->IP.reg = *((PtrSize *) (stack->stack + regs->SP.reg));

    // Restore BP
    regs->SP.reg -= sizeof(void *);
    regs->BP.reg = *((PtrSize *) (stack->stack + regs->SP.reg));

    // Restore the previous fiber context saved on the stack
    regs->SP.reg -= sizeof(FiberContext);
    stratum::util::MemoryCopy(&fiber->context.context, stack->stack + regs->SP.reg, sizeof(FiberContext));

    // Remove the generator parameters from the stack
    regs->SP.reg -= gen->stack - gen->params;

    // Dump the current registers into the generator
    stratum::util::MemoryCopy(gen->regs_dump, regs, kGeneralPurposeRegistersCount);

    gen->acquired = 0;
}

void StoreToObjectProp(const Fiber *fiber, const Function *func, OObject *obj, OObject *value,
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
        char error[24];

        GetTypeName(fiber->isolate, obj, error, sizeof(error));

        ErrorSet(fiber->isolate,
                 AttributeError::Details[AttributeError::Reason::ID],
                 nullptr,
                 AttributeError::Details[AttributeError::Reason::NOT_FOUND],
                 error,
                 (const char *) key->buffer);

        return;
    }

    if (ENUMBITMASK_ISFALSE(prop->detail, PropertyFlag::IS_PUBLIC)) {
        if (func == nullptr
            || func->shared->owner_type == nullptr
            || ENUMBITMASK_ISFALSE(prop->detail, PropertyFlag::IS_PROTECTED)
            || !IsTypeExtends(type, func->shared->owner_type)) {
            ErrorSetWithObjType(fiber->isolate,
                                AttributeError::Details[AttributeError::Reason::ID],
                                AttributeError::Details[AttributeError::Reason::PRIVATE_ACCESS],
                                (const char *) key->buffer, obj);

            return;
        }
    }

    if (ENUMBITMASK_ISTRUE(prop->detail, PropertyFlag::IS_CONSTANT)) {
        ErrorSet(fiber->isolate,
                 AttributeError::Details[AttributeError::Reason::ID],
                 nullptr,
                 AttributeError::Details[AttributeError::Reason::CONSTANT_ASSIGN],
                 (const char *) key->buffer);

        return;
    }

    if (ENUMBITMASK_ISTRUE(prop->detail, PropertyFlag::IN_OBJECT)) {
        auto *slot = O_SLOT(obj, target_type);

        slot[prop->slot] = value;
    }
}

OObject *orbiter::eval(Fiber *fiber) {
#define TARGET_OP(op)   case OPCode::op:
#define CGOTO           continue
#define DISPATCH        \
NEXT_IP;                \
CGOTO

#define FETCH                   (*((MachineWord *) regs->IP.reg))
#define FETCH_OP(instr)         ((OPCode) (instr >> 24))

#define NEXT_IP                 (regs->IP.reg += sizeof(MachineWord))
#define JMP_TO(offset)          (regs->IP.reg = (PtrSize) code->m_code + offset)

#define REGISTER_PTR(registers, n)  ((PtrSize *) (((Register *) (registers)) + n))
#define REGISTER(registers, n)      (*REGISTER_PTR(registers, n))

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
#define FETCH_J_SRC(instr)          FETCH_R_DST(instr)
#define FETCH_IMM(instr)            ((instr) & 0xFFFFu)
#define FETCH_SMI_8BIT(instr)       (O_TO_SMI(((instr) & 0xFFu)))

#define ACCESS_REG_DST(inst)        (REG_N(FETCH_R_DST(inst)))
#define ACCESS_REG_SRC(inst)        (REG_N(FETCH_R_SRC(inst)))

#define ACCESS_STACK_BP(offset) ((PtrSize *)(stack->stack + (regs->BP.reg + (offset))))
#define ACCESS_STACK_SP(offset) ((PtrSize *)(stack->stack + (regs->SP.reg + (offset))))
#define LOAD_FROM_STACK         ACCESS_STACK_SP((-sizeof(void *)))
#define CHECK_PREEMPT           do {if(--fiber->vm.preempt_tick == 0) {fiber->state = FiberState::YIELDED; return nullptr;}} while(0)

    auto *regs = &fiber->vm.regs;
    auto *stack = &fiber->vm.stack;

    OObject *result;

    U8 dst;
    U8 src;

    fiber->state = FiberState::RUNNING;

BEGIN:
    auto *code = fiber->context.code;
    if (code == nullptr)
        return (OObject *) regs->RR.reg;

    auto *this_func = (Function *) fiber->context.func;
    if (this_func != nullptr && O_IS_TYPE(this_func, InstanceType::GENERATOR))
        this_func = ((Generator *) this_func)->base;

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
                const auto flag = (AddSubFlags) ((instr >> 8) & 0xFu);

                if (!ObjectAdd(
                    fiber->isolate,
                    (OObject *) ACCESS_REG_SRC(instr),
                    (OObject *) ((flag == AddSubFlags::IMM8) ? FETCH_SMI_8BIT(instr) : REG_N(FETCH_R_RSRC(instr))),
                    result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(SUB) {
                const auto flag = (AddSubFlags) ((instr >> 8) & 0xFu);

                if (!ObjectSub(
                    fiber->isolate,
                    (OObject *) ACCESS_REG_SRC(instr),
                    (OObject *) ((flag == AddSubFlags::IMM8) ? FETCH_SMI_8BIT(instr) : REG_N(FETCH_R_RSRC(instr))),
                    result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(MUL) {
                const auto flag = (AddSubFlags) ((instr >> 8) & 0xFu);

                if (!ObjectMul(
                    fiber->isolate,
                    (OObject *) ACCESS_REG_SRC(instr),
                    (OObject *) ((flag == AddSubFlags::IMM8) ? FETCH_SMI_8BIT(instr) : REG_N(FETCH_R_RSRC(instr))),
                    result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(DIV) {
                const auto flag = (DivFlags) ((instr >> 8) & 0xFu);
                auto right = (OObject *) REG_N(FETCH_R_RSRC(instr));

                if (ENUMBITMASK_ISTRUE(flag, DivFlags::IMM8))
                    right = (OObject *) ((PtrSize) FETCH_SMI_8BIT(instr));

                if (flag == DivFlags::NONE) {
                    if (!ObjectIDiv(fiber->isolate, (OObject *) ACCESS_REG_SRC(instr), right, result))
                        goto ERROR;
                } else {
                    if (!ObjectDiv(fiber->isolate, (OObject *) ACCESS_REG_SRC(instr), right, result))
                        goto ERROR;
                }

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(MOD) {
                const auto flag = (DivFlags) ((instr >> 8) & 0xFu);
                auto right = (OObject *) REG_N(FETCH_R_RSRC(instr));

                if (ENUMBITMASK_ISTRUE(flag, DivFlags::IMM8))
                    right = (OObject *) ((PtrSize) FETCH_SMI_8BIT(instr));

                if (ENUMBITMASK_ISTRUE(flag, DivFlags::FLOAT)) {
                    if (!ObjectModR(fiber->isolate, (OObject *) ACCESS_REG_SRC(instr), right, result))
                        goto ERROR;
                } else {
                    if (!ObjectMod(fiber->isolate, (OObject *) ACCESS_REG_SRC(instr), right, result))
                        goto ERROR;
                }

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(AND) {
                if (!ObjectAnd(fiber->isolate,
                               (OObject *) ACCESS_REG_SRC(instr),
                               (OObject *) REG_N(FETCH_R_RSRC(instr)),
                               result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(OR) {
                if (!ObjectOr(fiber->isolate,
                              (OObject *) ACCESS_REG_SRC(instr),
                              (OObject *) REG_N(FETCH_R_RSRC(instr)),
                              result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(XOR) {
                if (!ObjectXor(fiber->isolate,
                               (OObject *) ACCESS_REG_SRC(instr),
                               (OObject *) REG_N(FETCH_R_RSRC(instr)),
                               result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(SHLR) {
                if (!ObjectLShift(fiber->isolate,
                                  (OObject *) ACCESS_REG_SRC(instr),
                                  (OObject *) REG_N(FETCH_R_RSRC(instr)),
                                  result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(SHLI) {
                if (!ObjectLShift(fiber->isolate,
                                  (OObject *) ACCESS_REG_SRC(instr),
                                  (OObject *) ((PtrSize) O_TO_SMI(FETCH_IMM(instr))),
                                  result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(SHRR) {
                if (!ObjectRShift(fiber->isolate,
                                  (OObject *) ACCESS_REG_SRC(instr),
                                  (OObject *) REG_N(FETCH_R_RSRC(instr)),
                                  result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(SHRI) {
                if (!ObjectRShift(fiber->isolate,
                                  (OObject *) ACCESS_REG_SRC(instr),
                                  (OObject *) ((PtrSize) O_TO_SMI(FETCH_IMM(instr))),
                                  result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(MEMB) {
                const auto flags = (MembershipFlags) ((instr >> 8) & 0xFu);
                bool out;

                if (!ObjectContains(fiber->isolate,
                                    (OObject *) REG_N(FETCH_R_SRC(instr)),
                                    (OObject *) REG_N(FETCH_R_RSRC(instr)),
                                    out))
                    goto ERROR;

                if (flags == MembershipFlags::NOT_IN)
                    out = !out;

                ACCESS_REG_DST(instr) = (PtrSize) BOOL_TO_OBOOL(out);

                DISPATCH;
            }
            TARGET_OP(EQ) {
                const auto src_l = FETCH_R_SRC(instr);
                const auto src_r = FETCH_R_RSRC(instr);
                const auto flags = (EqualityMode) ((instr >> 8) & 0xFu);
                bool res;

                if (flags == EqualityMode::NORMAL)
                    res = Equal((const OObject *) REG_N(src_l), (const OObject *) REG_N(src_r));
                else
                    res = EqualStrict((const OObject *) REG_N(src_l), (const OObject *) REG_N(src_r));

                ACCESS_REG_DST(instr) = BOOL_TO_OBOOL(res);

                DISPATCH;
            }
            TARGET_OP(MVN) {
                if (!ObjectMoveNot(fiber->isolate, (OObject *) ACCESS_REG_SRC(instr), result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(NEG) {
                if (!ObjectNeg(fiber->isolate, (OObject *) ACCESS_REG_SRC(instr), result))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(NOT) {
                auto *value = (OObject *) ACCESS_REG_SRC(instr);

                ACCESS_REG_DST(instr) = ((MSize) value == kOddBallTRUE || O_IS_OBJECT(value))
                                            ? kOddBallFALSE
                                            : kOddBallTRUE;

                DISPATCH;
            }
            TARGET_OP(PANIC) {
                const auto value = (OObject *) ACCESS_REG_SRC(instr);

                if (!O_IS_OBJECT(value) || !O_IS_TYPE(value, InstanceType::ERROR)) {
                    ErrorSet(fiber->isolate,
                             TypeError::Details[TypeError::Reason::ID],
                             nullptr,
                             TypeError::Details[(int) TypeError::Reason::PANIC],
                             fiber->isolate->primitive[(int) InstanceType::ERROR]->name);

                    goto ERROR;
                }

                fiber->Panic(value);

                goto ERROR;
            }
            TARGET_OP(RET) {
                const auto pops = instr & 0xFFFF;

                REG_RR = ACCESS_REG_SRC(instr);

                Return(fiber, pops);

                goto BEGIN;
            }
            TARGET_OP(RETSUB) {
                REG_RR = ACCESS_REG_SRC(instr);

                REG_BP -= sizeof(void *);
                REG_IP = *ACCESS_STACK_BP(0) + sizeof(MachineWord);

                REG_BP -= sizeof(void *);
                REG_SP = REG_BP;

                REG_BP = *ACCESS_STACK_SP(0);

                REG_SP -= sizeof(void *);

                fiber->context.code = (Code *) *ACCESS_STACK_SP(0);

                code = fiber->context.code;

                continue;
            }
            TARGET_OP(YLD) {
                REG_RR = ACCESS_REG_SRC(instr);

                SaveGenerator(fiber);

                goto BEGIN;
            }
            TARGET_OP(AWAIT) {
                auto *future = (Future *) ACCESS_REG_SRC(instr);

                if (!O_IS_OBJECT(future) || !O_IS_TYPE(future, InstanceType::FUTURE)) {
                    ErrorSetWithObjType(fiber->isolate,
                                        TypeError::Details[TypeError::Reason::ID],
                                        TypeError::Details[TypeError::Reason::MISMATCH],
                                        fiber->isolate->primitive[(int) InstanceType::FUTURE]->name,
                                        (OObject *) future);

                    goto ERROR;
                }

                if (FutureAsyncAwait(future)) {
                    fiber->state = FiberState::SUSPENDED;

                    return nullptr;
                }

                if (future->state == FutureState::REJECTED) {
                    fiber->Panic(future->result);

                    goto ERROR;
                }

                ACCESS_REG_DST(instr) = (PtrSize) future->result;

                DISPATCH;
            }
            TARGET_OP(CALL) {
                const auto flags = FETCH_F_DST(CallMode, instr);
                const auto p_count = FETCH_IMM(instr);
                const auto SP = regs->SP.reg;

                auto func = (Function *) ACCESS_REG_SRC(instr);

                int res;

                if (func != nullptr && O_IS_TYPE(func, InstanceType::GENERATOR)) {
                    res = CallGenerator(fiber, (Generator *) func, p_count, flags, true);
                    if (res == (int) CallResult::ERROR)
                        goto ERROR;

                    if (res == (int) CallResult::BUSY) {
                        fiber->state = FiberState::YIELDED;
                        return nullptr;
                    }

                    goto BEGIN;
                }

                res = CallInit(fiber, func, p_count, flags);
                if (res == (int) CallResult::ERROR)
                    goto ERROR;

                if (res == (int) CallResult::DONE) {
                    DISPATCH;
                }

                if (func->shared->IsGenerator()) {
                    const auto param_size = (REG_SP - SP) + p_count * sizeof(void *);
                    REG_RR = (PtrSize) GeneratorNew(fiber, func, param_size).get();

                    // Release the stack; the call is complete, and the parameters have been copied,
                    // ready for later execution
                    REG_SP = SP - (p_count * sizeof(void *));

                    DISPATCH;
                }

                if (Call(fiber, func, res)) {
                    CHECK_PREEMPT;

                    goto BEGIN;
                }

                DISPATCH;
            }
            TARGET_OP(NTCALL) {
                const auto p_count = FETCH_IMM(instr);
                const auto old_SP = regs->SP.reg - (p_count * sizeof(void *));

                const auto func = (Function *) ACCESS_REG_SRC(instr);

                if (!O_IS_OBJECT(func) || !O_IS_TYPE(func, InstanceType::NATIVE_FUNC)) {
                    ErrorSetWithObjType(fiber->isolate,
                                        TypeError::Details[TypeError::Reason::ID],
                                        TypeError::Details[TypeError::Reason::NON_CALLABLE],
                                        nullptr,
                                        (OObject *) func);

                    goto ERROR;
                }

                HOObject res;

                if (!native::CallFunction(fiber->isolate,
                                          res,
                                          (NativeFunc *) func,
                                          (OObject **) (stack->stack + old_SP),
                                          p_count))
                    goto ERROR;

                REG_RR = (PtrSize) res.get();

                regs->SP.reg = old_SP;

                DISPATCH;
            }
            TARGET_OP(SPWN) {
                const auto flags = FETCH_F_DST(CallMode, instr);
                const auto p_count = FETCH_IMM(instr);

                auto func = (Function *) ACCESS_REG_SRC(instr);

                if (func != nullptr && O_IS_TYPE(func, InstanceType::GENERATOR)) {
                    ErrorSet(fiber->isolate,
                             TypeError::Details[TypeError::Reason::ID],
                             nullptr,
                             TypeError::Details[TypeError::Reason::GENERATOR_SPAWN]);

                    goto ERROR;
                }

                auto res = CallInit(fiber, func, p_count, flags);
                if (res == (int) CallResult::ERROR)
                    goto ERROR;

                const auto size = res * sizeof(void *);
                if (!Orbiter::GetInstance()->EvalAsync(func,
                                                       (fiber->vm.stack.stack + regs->SP.reg) - size,
                                                       size).get())
                    goto ERROR;

                regs->RR.reg = 0;
                regs->SP.reg -= size;

                DISPATCH;
            }
            TARGET_OP(DEFER) {
                const auto flags = FETCH_F_DST(CallMode, instr);
                const auto p_count = FETCH_IMM(instr);

                auto func = (Function *) ACCESS_REG_SRC(instr);

                const auto res = CallInit(fiber, func, p_count, flags);
                if (res == (int) CallResult::ERROR)
                    goto ERROR;

                auto *defer = fiber->isolate->dpool_->NewDefer();
                if (defer == nullptr)
                    goto ERROR;

                defer->func = func;

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
                const auto sproc = (Code *) ACCESS_REG_SRC(instr);

                if (!stack->Check(fiber->isolate, regs->SP.reg, sproc->stack_size + (3 * sizeof(PtrSize))))
                    goto ERROR;

                fiber->vm.Push((OObject *) code);
                fiber->vm.Push(regs->BP.reg);
                fiber->vm.Push(regs->IP.reg);

                regs->BP.reg = regs->SP.reg;

                fiber->context.code = sproc;
                code = sproc;

                regs->IP.reg = (PtrSize) sproc->m_code;

                continue;
            }
            TARGET_OP(LDCODE) {
                const auto imm = FETCH_IMM(instr);

                ACCESS_REG_DST(instr) = (PtrSize) code->codes->objects[imm];

                DISPATCH;
            }
            TARGET_OP(LDCST) {
                const auto flags = (LoadConstantMode) FETCH_R_SRC(instr);
                const auto imm = FETCH_IMM(instr);

                if (flags == LoadConstantMode::OFFSET)
                    ACCESS_REG_DST(instr) = (PtrSize) code->static_resources->objects[imm];
                else if (flags == LoadConstantMode::TRUE)
                    ACCESS_REG_DST(instr) = kOddBallTRUE;
                else if (flags == LoadConstantMode::FALSE)
                    ACCESS_REG_DST(instr) = kOddBallFALSE;
                else if (flags == LoadConstantMode::NIL)
                    ACCESS_REG_DST(instr) = (PtrSize) kOddBallNIL;

                DISPATCH;
            }
            TARGET_OP(LDIMM) {
                const auto imm = FETCH_IMM(instr);
                const auto shift = (instr >> 16) & 0x0F;

                dst = FETCH_R_DST(instr);

                REG_N(dst) = shift == 0 ? O_TO_SMI(imm) : O_TO_SMI(O_FROM_SMI(REG_N(dst)) | (imm << (16 * shift)));

                DISPATCH;
            }
            TARGET_OP(SETPROP) {
                const auto offset = FETCH_IMM(instr);

                auto *tp = (TypeInfo *) ACCESS_REG_DST(instr);
                const auto *key = (ORString *) code->unknown_symbols->objects[offset];
                auto *value = (OObject *) ACCESS_REG_SRC(instr);

                auto prop = TIFindLocalProperty(tp, (const char *) key->buffer);
                if (prop == nullptr) {
                    // FIXME: error
                    assert(false);
                }

                assert(prop->value==nullptr);

                if (O_IS_OBJECT(value)
                    && O_IS_TYPE(value, InstanceType::FUNCTION)
                    && ((Function *) value)->shared->IsMethod())
                    ((Function *) value)->shared->owner_type = tp;

                prop->value = value;

                DISPATCH;
            }
            TARGET_OP(NGBLV) {
                const auto flags = FETCH_F_DST(VariableFlags, instr);
                const auto value = ACCESS_REG_SRC(instr);
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
                const auto value = ACCESS_REG_SRC(instr);
                const auto k_index = FETCH_IMM(instr);

                if (!ContextSet(fiber->context.context,
                                (ORString *) code->unknown_symbols->objects[k_index],
                                (OObject *) value)) {
                    // TODO: Error!
                }

                DISPATCH;
            }
            TARGET_OP(STGOFF) {
                const auto value = ACCESS_REG_SRC(instr);
                const auto slot = FETCH_IMM(instr);

                assert(module_slots != nullptr);

                *(module_slots + slot) = (OObject *) value;

                DISPATCH;
            }
            TARGET_OP(STRES) {
                if (fiber->panic.current_ == nullptr) {
                    result = (OObject *) ACCESS_REG_SRC(instr);

                    ACCESS_REG_DST(instr) = (PtrSize) ResultNew(fiber->isolate, result, true).get();
                } else {
                    result = fiber->GetDiscardPanic().release();

                    ACCESS_REG_DST(instr) = (PtrSize) ResultNew(fiber->isolate, result, false).get();
                }

                DISPATCH;
            }
            TARGET_OP(LDCLO) {
                const auto base = FETCH_R_DST(instr);
                const auto slot = ((short) FETCH_IMM(instr)) * (short) sizeof(void *);
                const auto value = (OObject *) this_func->closure;
                const auto target = (OObject **) (stack->stack + (REG_N(base) + slot));

                *target = value;

                DISPATCH;
            }
            TARGET_OP(LDGBL) {
                HOObject out;

                // Before checking the context, VM attempts to load data from the module itself (if it exists)
                /* FIXME
                 *if (fiber->context.module != nullptr) {
                    result = LoadFromObjectProp(fiber, nullptr, (OObject *) fiber->context.module, {},
                                                FETCH_IMM(instr));
                    if (result != nullptr) {
                        ACCESS_REG_DST(instr) = (PtrSize) result;

                        DISPATCH;
                    }
                }*/

                if (!ContextLookup(fiber->context.context,
                                   (ORString *) code->unknown_symbols->objects[FETCH_IMM(instr)],
                                   out,
                                   nullptr))
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) out.get();

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
                auto slot = ((short) FETCH_IMM(instr)) * (short) sizeof(void *);

                ACCESS_REG_DST(instr) = *((PtrSize *) (stack->stack + (REG_N(base) + slot)));

                DISPATCH;
            }
            TARGET_OP(SKSTR) {
                const auto base = FETCH_R_DST(instr);
                auto slot = ((short) FETCH_IMM(instr)) * (short) sizeof(void *);
                auto value = (OObject *) ACCESS_REG_SRC(instr);

                const auto target = (OObject **) (stack->stack + (REG_N(base) + slot));

                *target = value;

                DISPATCH;
            }
            TARGET_OP(PUSH) {
                auto value = (OObject *) ACCESS_REG_SRC(instr);

                if (!stack->Check(fiber->isolate, regs->SP.reg, sizeof(void *)))
                    goto ERROR;

                *ACCESS_STACK_SP(0) = (PtrSize) value;
                REG_SP += sizeof(void *);

                DISPATCH;
            }
            TARGET_OP(PUSHIF) {
                const auto value = (OObject *) ACCESS_REG_DST(instr);
                const auto target = (OObject *) ACCESS_REG_SRC(instr);
                const auto flags = (PushIfFlags) (instr & 0xFu);

                if (flags == PushIfFlags::METHOD) {
                    if (!O_IS_OBJECT(target) || !O_IS_TYPE(target, InstanceType::FUNCTION)) {
                        ErrorSetWithObjType(fiber->isolate,
                                            TypeError::Details[TypeError::Reason::ID],
                                            TypeError::Details[TypeError::Reason::NON_CALLABLE],
                                            nullptr,
                                            target);

                        goto ERROR;
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

                DISPATCH;
            }
            TARGET_OP(POP) {
                const auto target = (OObject **) LOAD_FROM_STACK;
                auto value = *target;

                *target = nullptr;

                REG_SP -= sizeof(void *);

                ACCESS_REG_DST(instr) = (PtrSize) value;

                DISPATCH;
            }
            TARGET_OP(POPN) {
                REG_SP -= FETCH_IMM(instr) * sizeof(void *);

                DISPATCH;
            }
            TARGET_OP(CLONEW) {
                const auto slots = instr & 0xFFFF;

                auto closure = ClosureNew(fiber->isolate, slots);
                if (!closure)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) closure.get();

                DISPATCH;
            }
            TARGET_OP(CLOLDR) {
                auto slot = FETCH_IMM(instr);

                auto *closure = *(Closure **) (stack->stack + regs->BP.reg + (code->vars_count * sizeof(void *)));

                ACCESS_REG_DST(instr) = (PtrSize) ClosureGet(closure, slot).get();

                DISPATCH;
            }
            TARGET_OP(CLOSTR) {
                auto slot = FETCH_IMM(instr);

                auto *closure = *(Closure **) (stack->stack + regs->BP.reg + (code->vars_count * sizeof(void *)));

                ClosureSet(closure, slot, (OObject *) ACCESS_REG_SRC(instr));

                DISPATCH;
            }
            TARGET_OP(CHRCV) {
                auto status = ChannelRecv((Channel *) REG_N(FETCH_R_SRC(instr)), result);

                if (status == ChannelRecvStatus::BLOCKED) {
                    fiber->state = FiberState::SUSPENDED;

                    return nullptr;
                }

                if (status == ChannelRecvStatus::CLOSED)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(CHSND) {
                ChannelSendStatus status;

                if (!ChannelSend(fiber->isolate,
                                 (Channel *) REG_N(FETCH_R_DST(instr)),
                                 (OObject *) REG_N(FETCH_R_SRC(instr)),
                                 status))
                    goto ERROR;

                if (status == ChannelSendStatus::BLOCKED) {
                    fiber->state = FiberState::SUSPENDED;

                    return nullptr;
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
                const auto flags = (LoadFuncFlags) (instr & 0xFFF);

                auto *closure = *(Closure **) (stack->stack + regs->BP.reg + (code->vars_count * sizeof(void *)));
                Tuple *defs = nullptr;

                if (ENUMBITMASK_ISTRUE(flags, LoadFuncFlags::NPARAMS))
                    defs = (Tuple *) REG_N(FETCH_R_RSRC(instr));

                auto func = FunctionNew((Code *) ACCESS_REG_SRC(instr), closure, defs, flags);
                if (!func)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) func.get();

                DISPATCH;
            }
            TARGET_OP(LDMOD) {
                assert(false);

                DISPATCH;
            }
            TARGET_OP(LDNAT) {
                const auto imm = FETCH_IMM(instr);

                auto sym = fiber->isolate->loader_->Load(code->native.bindings + imm);
                if (!sym)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) sym.get();

                DISPATCH;
            }
            TARGET_OP(NDICT) {
                // const auto imm = FETCH_IMM(instr);

                auto dict = DictNew(fiber->isolate);
                if (!dict)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) dict.get();

                DISPATCH;
            }
            TARGET_OP(NERROR) {
                const auto kind = (Atom *) ACCESS_REG_SRC(instr);
                const auto reason = (ORString *) REG_N(FETCH_R_RSRC(instr));
                const auto details = (OObject *) REG_N(((instr >> 8) & 0xFu));

                const auto err = ErrorNew(fiber->isolate, kind, reason, details);

                ACCESS_REG_DST(instr) = (PtrSize) err.get();

                DISPATCH;
            }
            TARGET_OP(NLIST) {
                const auto imm = FETCH_IMM(instr);

                auto list = ListNew(fiber->isolate, imm);
                if (!list)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) list.get();

                DISPATCH;
            }
            TARGET_OP(NSET) {
                dst = FETCH_R_DST(instr);
                const auto imm = FETCH_IMM(instr);

                // TODO: Set here!
                assert(false);

                DISPATCH;
            }
            TARGET_OP(NTUPLE) {
                const auto imm = FETCH_IMM(instr);

                auto tuple = TupleNew(fiber->isolate, imm);
                if (!tuple)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) tuple.get();

                DISPATCH;
            }
            TARGET_OP(ADDELEM) {
                auto *obj = (OObject *) ACCESS_REG_DST(instr);
                auto *value = (OObject *) ACCESS_REG_SRC(instr);

                assert(O_IS_OBJECT(obj));

                if (O_IS_TYPE(obj, InstanceType::DICT)) {
                    if (!DictInsert((Dict *) obj, value, (OObject *) REG_N(FETCH_R_RSRC(instr))))
                        goto ERROR;
                } else if (O_IS_TYPE(obj, InstanceType::LIST))
                    ListAppend((List *) obj, value);
                else if (O_IS_TYPE(obj, InstanceType::TUPLE))
                    TupleAppend((Tuple *) obj, value);
                else
                    assert(false);

                DISPATCH;
            }
            TARGET_OP(LDIDX) {
                if (!LoadFromIndex(fiber,
                                   (OObject *) REG_N(FETCH_R_SRC(instr)),
                                   (OObject *) REG_N(FETCH_R_RSRC(instr)),
                                   *REGISTER_PTR(regs, FETCH_R_DST(instr))))
                    goto ERROR;

                DISPATCH;
            }
            TARGET_OP(STIDX) {
                if (!StoreToIndex(fiber,
                                  (OObject *) REG_N(FETCH_R_DST(instr)),
                                  (OObject *) REG_N(FETCH_R_SRC(instr)),
                                  (OObject *) REG_N(FETCH_R_RSRC(instr))))
                    goto ERROR;


                DISPATCH;
            }
            TARGET_OP(LDSBSCR) {
                if (!LoadFromSlice(fiber,
                                   (OObject *) REG_N(FETCH_R_SRC(instr)),
                                   (OObject *) REG_N(FETCH_R_RSRC(instr)),
                                   (OObject *) REG_N(((instr >> 8) & 0xFu)),
                                   (OObject *) REG_N(((instr >> 4) & 0xFu)),
                                   *REGISTER_PTR(regs, FETCH_R_DST(instr))))
                    goto ERROR;

                DISPATCH;
            }
            TARGET_OP(LDOBJP) {
                const auto obj = (OObject *) ACCESS_REG_SRC(instr);
                const auto flags = (LoadObjectPropFlags) FETCH_R_RSRC(instr);
                const auto offset = instr & 0xFFF;

                // Fast path
                if (((int) flags & 1) == (int) LoadObjectPropFlags::INLINE) {
                    auto *slot = O_SLOT(obj, this_func->shared->owner_type);

                    ACCESS_REG_DST(instr) = (PtrSize) slot[offset];

                    DISPATCH;
                }

                result = LoadFromObjectProp(fiber, this_func, obj, flags, offset);
                if (fiber->panic.current_ != nullptr)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) result;

                DISPATCH;
            }
            TARGET_OP(STOBJP) {
                const auto obj = (OObject *) ACCESS_REG_DST(instr);
                const auto value = (OObject *) ACCESS_REG_SRC(instr);
                const auto flags = (LoadObjectPropFlags) FETCH_R_RSRC(instr);
                const auto offset = instr & 0xFFF;

                // Fast path
                if (flags == LoadObjectPropFlags::INLINE) {
                    auto *slot = O_SLOT(obj, this_func->shared->owner_type);

                    slot[offset] = value;

                    DISPATCH;
                }

                StoreToObjectProp(fiber, this_func, obj, value, flags, offset);
                if (fiber->panic.current_ != nullptr)
                    goto ERROR;

                DISPATCH;
            }
            TARGET_OP(MKCLZ) {
                const auto flags = FETCH_F_SRC(ClassFlags, instr);
                const auto impls = FETCH_IMM(instr);

                auto clazz = ClassTypeNew(code,
                                          flags == ClassFlags::EXTEND
                                              ? (TypeInfo *) *ACCESS_STACK_SP(-((impls+1) * sizeof(void*)))
                                              : nullptr,
                                          (TypeInfo **) ACCESS_STACK_SP(-(impls * sizeof(void*))),
                                          impls);
                if (!clazz)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) clazz.get();

                DISPATCH;
            }
            TARGET_OP(MKTRT) {
                const auto impls = FETCH_IMM(instr);

                auto trait = TraitTypeNew(code, (TypeInfo **) ACCESS_STACK_SP(-(impls * sizeof(void*))), impls);
                if (!trait)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) trait.get();

                DISPATCH;
            }
            TARGET_OP(NOBJ) {
                auto *tp = (TypeInfo *) ACCESS_REG_SRC(instr);

                auto instance = ClassNew(tp);
                if (!instance)
                    goto ERROR;

                ACCESS_REG_DST(instr) = (PtrSize) instance.get();

                DISPATCH;
            }
            TARGET_OP(GITR) {
                dst = FETCH_R_DST(instr);

                if (!VMGetIter(fiber, (OObject *) ACCESS_REG_SRC(instr), REGISTER_PTR(regs, dst)))
                    goto ERROR;

                DISPATCH;
            }
            TARGET_OP(ITRNXT) {
                dst = FETCH_R_DST(instr);
                const auto jmp = FETCH_IMM(instr);

                auto res = (CallResult)
                        VMGetIterNext(fiber, (OObject *) ACCESS_REG_SRC(instr), REGISTER_PTR(regs, dst));
                if (res == CallResult::ERROR)
                    goto ERROR;
                if (res == CallResult::CONTINUE)
                    goto BEGIN;
                if (res == CallResult::EXHAUST) {
                    JMP_TO(jmp);

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(JEN) {
                const auto offset = instr & 0x1FFFFF;

                if (REG_N(FETCH_J_SRC(instr)) == (PtrSize) kOddBallNIL) {
                    JMP_TO(offset);

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(JERR) {
                const auto offset = instr & 0x1FFFFF;

                auto *ec = (ExceptionContext *) regs->CP.reg;
                auto *e_key = (Atom *) REG_N(FETCH_J_SRC(instr));

                if (e_key == nullptr || ((Error *) fiber->panic.current_->error)->kind == e_key) {
                    // Store error in current exception context
                    ec->ret_value = (PtrSize) fiber->GetDiscardPanic().release();

                    // Mark catch handler as consumed
                    ec->coffset = 0;

                    JMP_TO(offset);

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(JF) {
                const auto offset = instr & 0x1FFFFF;

                if (REG_N(FETCH_J_SRC(instr)) == kOddBallFALSE) {
                    JMP_TO(offset);

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(JT) {
                const auto offset = instr & 0x1FFFFF;

                if (REG_N(FETCH_J_SRC(instr)) == kOddBallTRUE) {
                    JMP_TO(offset);

                    continue;
                }

                DISPATCH;
            }
            TARGET_OP(JMP) {
                const auto offset = instr & 0xFFFFFFu;

                CHECK_PREEMPT;

                JMP_TO(offset);

                continue;
            }
            TARGET_OP(SYNC_ENTER) {
                result = (OObject *) ACCESS_REG_SRC(instr);

                if (!O_IS_OBJECT(result)) {
                    ErrorSetWithObjType(fiber->isolate,
                                        TypeError::Details[TypeError::Reason::ID],
                                        TypeError::Details[TypeError::Reason::NON_SYNCHRONIZABLE],
                                        nullptr,
                                        result);
                    goto ERROR;
                }

                const auto res = MonitorAcquire(fiber, result);
                if (res < 0)
                    goto ERROR;

                if (res == 0) {
                    fiber->state = FiberState::YIELDED;
                    return nullptr;
                }

                DISPATCH;
            }
            TARGET_OP(SYNC_EXIT) {
                MonitorRelease((OObject *) ACCESS_REG_SRC(instr));

                DISPATCH;
            }
            TARGET_OP(TBGIN) {
                const auto coffset = (instr & 0x3FFFFu);
                const auto slot = ((instr >> 18) & 0x3Fu) * sizeof(void *);

                const auto ctx = (ExceptionContext *) (stack->stack + (regs->BP.reg + slot));

                ctx->_sentinel_ = kExceptionContextTag;
                ctx->prev = (ExceptionContext *) regs->CP.reg;

                ctx->ret_pops = 0;
                ctx->action = (U32) PendingAction::NONE;

                ctx->coffset = coffset;
                ctx->foffset = 0;

                ctx->ret_value = 0;

                ctx->key = regs->BP.reg;

                regs->CP.reg = (PtrSize) ctx;

                DISPATCH;
            }
            TARGET_OP(TEND) {
                const auto ctx = (ExceptionContext *) regs->CP.reg;

                if (fiber->panic.current_ != nullptr) {
                    ReleaseExceptionContext(regs);

                    goto ERROR;
                }

                if (ctx->action == (U32) PendingAction::RETURN) {
                    if (ExecDefer(fiber))
                        goto BEGIN;

                    REG_RR = ctx->ret_value;

                    ReleaseExceptionContext(regs);

                    Return(fiber, ctx->ret_pops);

                    goto BEGIN;
                }

                if (ctx->action != (U32) PendingAction::NONE) {
                    U32 target = ctx->ret_pops;
                    U32 action = ctx->action;

                    ReleaseExceptionContext(regs);

                    auto *outer = (ExceptionContext *) regs->CP.reg;
                    if (outer != nullptr && outer->key == regs->BP.reg) {
                        outer->action = action;
                        outer->ret_pops = target;

                        JMP_TO(outer->foffset);

                        goto BEGIN;
                    }

                    JMP_TO(target);
                    goto BEGIN;
                }

                ReleaseExceptionContext(regs);

                DISPATCH;
            }
            TARGET_OP(TSFIN) {
                const auto offset = instr & 0xFFFFFFu;
                auto *ec = (ExceptionContext *) regs->CP.reg;

                ec->foffset = offset;

                DISPATCH;
            }
            TARGET_OP(TSPA) {
                const auto action = (PendingAction) ((instr >> 22) & 0x3u);
                const auto offset = instr & 0x3FFFFu;

                src = ((instr >> 18) & 0xF);

                auto ctx = (ExceptionContext *) regs->CP.reg;
                ctx->action = (U32) action;
                ctx->ret_pops = offset;

                if (action == PendingAction::RETURN)
                    ctx->ret_value = REG_N(src);

                if (action != PendingAction::NONE)
                    ctx->ret_pops = offset;

                DISPATCH;
            }
            TARGET_OP(LDEXC) {
                ACCESS_REG_DST(instr) = ((ExceptionContext *) regs->CP.reg)->ret_value;

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
                ExecuteCleanupForPC(fiber);

                if (except->coffset != 0) {
                    JMP_TO(except->coffset);
                    except->coffset = 0;

                    goto CATCH_FINALLY;
                }

                if (except->foffset != 0) {
                    JMP_TO(except->foffset);
                    except->foffset = 0;

                    goto CATCH_FINALLY;
                }

                // Release the exhausted exception context (both catch and finally consumed)
                ReleaseExceptionContext(regs);
            }
        }
    }

    if (UnwindStack(fiber))
        goto BEGIN;

    return (OObject *) regs->RR.reg;
}
