// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/orbcall.h>

using namespace orbiter;
using namespace orbiter::datatype;

bool CallCtx::EnsureStack(Isolate *isolate) const {
    U32 stack_size_required = 0;

    if (this->fn_shared->IsInterpreted())
        stack_size_required = kStackPrologueOffset
                              + (this->fn_shared->code->stack_size * sizeof(void *))
                              + (2 * sizeof(void *));

    if (this->fn_shared->HasDefaultArgs())
        stack_size_required += (this->fn_shared->defaults->length / 2) * sizeof(void *);

    return this->stack->Check(isolate, this->regs->SP.reg, stack_size_required);
}

bool CallCtx::ExpandDefaultArgs(Fiber *fiber) {
    if (!this->fn_shared->HasDefaultArgs()) {
        if (this->call_mode_is_nargs) {
            ErrorSet(fiber->isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::NO_NAMED_ARGS],
                     ORSTRING_TO_CSTR(this->fn_shared->name));

            return false;
        }

        return true;
    }

    const auto defaults = this->fn_shared->defaults;
    for (auto i = 0; i < defaults->length; i += 2) {
        HOObject out;

        this->stack_args += 1;

        if (this->call_mode_is_nargs && DictLookup(this->nargs, defaults->objects[i], out) == LookupResult::OK) {
            fiber->vm.Push(out.get());

            continue;
        }

        if (this->call_mode_is_kwarg && DictLookup(this->kwargs, defaults->objects[i], out) == LookupResult::OK) {
            fiber->vm.Push(out.get());

            continue;
        }

        // Push default
        fiber->vm.Push(defaults->objects[i + 1]);
    }

    return true;
}

bool CallCtx::FinalizeKwargs(Fiber *fiber) {
    if (this->call_mode_is_kwarg) {
        if (ENUMBITMASK_ISFALSE(this->fn_shared->kind, FunctionKind::KWARGS)) {
            ErrorSet(fiber->isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::NO_KWARGS],
                     ORSTRING_TO_CSTR(this->fn_shared->name));

            return false;
        }

        fiber->vm.Push((OObject *) this->kwargs);

        return true;
    }

    if (this->fn_shared->IsKWargs()) {
        const auto dict = DictNew(fiber->isolate);
        if (!dict)
            return false;

        fiber->vm.Push((OObject *) dict.get());
    }

    return true;
}

bool CallCtx::FinalizeRestArgs(Fiber *fiber) {
    if (this->call_mode_is_rest) {
        fiber->vm.Push((OObject *) this->rest);

        return true;
    }

    if (this->fn_shared->IsVariadic()) {
        const auto list = ListNew(fiber->isolate, 0);
        if (!list)
            return false;

        fiber->vm.Push((OObject *) list.get());
    }

    return true;
}

bool CallCtx::NormalizeMethod(Isolate *isolate) {
    if (!this->fn_shared->IsMethod()) {
        this->stack_args -= 1;

        return true;
    }

    // Check object is instance. The receiver is the first argument of the frame.
    const auto receiver = *this->ArgsBase();
    const auto *type = GetTypeInfoFromObject(isolate, receiver);

    if (O_IS_OBJECT(receiver) && !O_GET_RC(receiver).IsInstance())
        type = O_GET_TYPE(type);

    if (!IsTypeExtends(type, this->fn_shared->owner_type)) {
        ErrorSetWithObjType(isolate,
                            TypeError::Details[TypeError::Reason::ID],
                            TypeError::Details[TypeError::Reason::METHOD_RECEIVER],
                            nullptr,
                            receiver);

        return false;
    }

    return true;
}

void CallCtx::LoadCurrying(const Function *func) {
    const auto args = this->ArgsBase();

    const auto offset = func->currying->length;

    for (auto i = 0; i < this->stack_args; i++)
        args[offset + i] = args[i];

    for (auto i = 0; i < func->currying->length; i++)
        args[i] = func->currying->objects[i];

    this->stack_args += func->currying->length;
    this->regs->SP.reg += (offset * sizeof(void *));
}

CallResult CallCtx::CallInit(Fiber *fiber, Function *&func, const U16 p_count, const CallMode mode) {
    auto *callee = ResolveCallable(fiber->isolate, func);
    if (callee == nullptr)
        return CallResult::ERROR;

    func = callee;

    this->regs = &fiber->vm.regs;
    this->stack = &fiber->vm.stack;
    this->fn_shared = func->shared;
    this->nargs = (Dict *) fiber->vm.regs.r10.reg;
    this->rest = (List *) fiber->vm.regs.r11.reg;
    this->kwargs = (Dict *) fiber->vm.regs.r12.reg;
    this->stack_args = p_count;
    this->call_mode_is_kwarg = ENUMBITMASK_ISTRUE(mode, CallMode::KW_ARG);
    this->call_mode_is_nargs = ENUMBITMASK_ISTRUE(mode, CallMode::NARGS);
    this->call_mode_is_rest = ENUMBITMASK_ISTRUE(mode, CallMode::REST_ARG);

    if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD) && !this->NormalizeMethod(fiber->isolate))
        return CallResult::ERROR;

    if (!this->EnsureStack(fiber->isolate)) {
        ErrorSet(fiber->isolate,
                 MemoryError::Details[MemoryError::Reason::ID],
                 nullptr,
                 MemoryError::Details[MemoryError::Reason::STACK]);

        return CallResult::ERROR;
    }

    bool rest_edited = false;
    bool currying_pushed = false;

    auto total_args = this->stack_args;
    if (func->currying != nullptr)
        total_args += func->currying->length;

    if (total_args < this->fn_shared->arity) {
        const auto args_diff = this->fn_shared->arity - total_args;

        if (!this->call_mode_is_rest || this->rest->length < args_diff) {
            if (func->shared->IsInit()) {
                ErrorSet(fiber->isolate,
                         TypeError::Details[TypeError::Reason::ID],
                         nullptr,
                         TypeError::Details[TypeError::Reason::INIT_NO_CURRY],
                         this->fn_shared->owner_type->name);

                return CallResult::ERROR;
            }

            this->regs->RR.reg = (PtrSize) FunctionNew(func, this->ArgsBase(), this->stack_args).get();
            this->regs->SP.reg -= this->StackArgsBytes();

            return CallResult::DONE;
        }

        if (!func->shared->IsVariadic() && this->rest->length > args_diff) {
            ErrorSet(fiber->isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::TOO_MANY_ARGS],
                     ORSTRING_TO_CSTR(this->fn_shared->name),
                     (int) this->fn_shared->arity,
                     (int) (this->stack_args + this->rest->length));

            return CallResult::ERROR;
        }

        if (func->currying != nullptr) {
            this->LoadCurrying(func);

            currying_pushed = true;
        }

        for (auto i = 0; i < args_diff; i++) {
            fiber->vm.Push(this->rest->objects[i]);

            this->stack_args += 1;
        }

        if (func->shared->IsVariadic()) {
            const auto tmp = ListNew(fiber->isolate, this->rest->length - args_diff);
            if (!tmp)
                return CallResult::ERROR;

            ListExtend(tmp.get(), this->rest->objects + args_diff, this->rest->length - args_diff);

            this->rest = tmp.get();

            rest_edited = true;
        }
    }

    if (!currying_pushed && func->currying != nullptr)
        this->LoadCurrying(func);

    if (this->call_mode_is_rest)
        total_args += this->rest->length;

    if (total_args > this->fn_shared->arity) {
        if (!func->shared->IsVariadic()) {
            ErrorSet(fiber->isolate,
                     TypeError::Details[TypeError::Reason::ID],
                     nullptr,
                     TypeError::Details[TypeError::Reason::TOO_MANY_ARGS],
                     ORSTRING_TO_CSTR(this->fn_shared->name),
                     (int) this->fn_shared->arity,
                     (int) total_args);

            return CallResult::ERROR;
        }

        if (!rest_edited) {
            const auto excess_on_stack = this->stack_args > this->fn_shared->arity
                                             ? this->stack_args - this->fn_shared->arity
                                             : 0;

            const auto rest_len = this->call_mode_is_rest ? this->rest->length : 0;

            const auto tmp = ListNew(fiber->isolate, excess_on_stack + rest_len);
            if (!tmp)
                return CallResult::ERROR;

            if (excess_on_stack > 0) {
                ListExtend(tmp.get(), this->StackTop(excess_on_stack), excess_on_stack);

                this->regs->SP.reg -= excess_on_stack * sizeof(void *);
                this->stack_args -= excess_on_stack;
            }

            if (this->call_mode_is_rest)
                ListExtend(tmp.get(), (OObject *) this->rest);

            this->rest = tmp.get();
        }

        this->call_mode_is_rest = true;
    }

    if (!this->ExpandDefaultArgs(fiber))
        return CallResult::ERROR;

    if (!this->FinalizeRestArgs(fiber))
        return CallResult::ERROR;

    if (!this->FinalizeKwargs(fiber))
        return CallResult::ERROR;

    return CallResult::OK;
}

Function *orbiter::ResolveCallable(Isolate *isolate, Function *func) {
    if (O_IS_OBJECT(func)) {
        if (O_IS_TYPE(func, InstanceType::FUNCTION))
            return func;

        // A type object is called through its constructor.
        if (O_IS_TYPE(func, InstanceType::TYPE) && ((TypeInfo *) func)->ctor != nullptr)
            return (Function *) ((TypeInfo *) func)->ctor;
    }

    ErrorSetWithObjType(isolate,
                        TypeError::Details[TypeError::Reason::ID],
                        TypeError::Details[TypeError::Reason::NON_CALLABLE],
                        nullptr,
                        (OObject *) func);

    return nullptr;
}