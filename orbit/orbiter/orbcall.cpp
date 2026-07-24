// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/orbcall.h>

using namespace orbiter;
using namespace orbiter::datatype;

/// Raise a TypeError; `args` fill the reason's format placeholders.
template<typename... Args>
static void SetTypeError(Isolate *isolate, const TypeError::Reason reason, Args... args) {
    ErrorSet(isolate,
             TypeError::Details[TypeError::Reason::ID],
             nullptr,
             TypeError::Details[reason],
             args...);
}

/// Raise a TypeError whose message names the type of the offending object.
static void SetTypeErrorWithObj(Isolate *isolate, const TypeError::Reason reason, const OObject *target) {
    ErrorSetWithObjType(isolate,
                        TypeError::Details[TypeError::Reason::ID],
                        TypeError::Details[reason],
                        nullptr,
                        target);
}

bool ArgumentBinder::EnsureStack() const {
    U32 stack_size_required = 0;

    if (this->func->shared->IsInterpreted())
        stack_size_required = kStackPrologueOffset
                              + (this->func->shared->code->stack_size * sizeof(void *))
                              + (2 * sizeof(void *));

    if (this->func->shared->HasDefaultArgs())
        stack_size_required += (this->func->shared->defaults->length / 2) * sizeof(void *);

    return this->fiber->vm.stack.Check(this->fiber->isolate, this->fiber->vm.regs.SP.reg, stack_size_required);
}

bool ArgumentBinder::ExpandDefaultArgs() {
    if (!this->func->shared->HasDefaultArgs()) {
        if (this->call_mode_is_nargs) {
            SetTypeError(this->fiber->isolate,
                         TypeError::Reason::NO_NAMED_ARGS,
                         ORSTRING_TO_CSTR(this->func->shared->name));

            return false;
        }

        return true;
    }

    const auto defaults = this->func->shared->defaults;
    for (auto i = 0; i < defaults->length; i += 2) {
        HOObject out;

        this->stack_args += 1;

        if (this->call_mode_is_nargs && DictLookup(this->nargs, defaults->objects[i], out) == LookupResult::OK) {
            this->fiber->vm.Push(out.get());

            continue;
        }

        if (this->call_mode_is_kwarg && DictLookup(this->kwargs, defaults->objects[i], out) == LookupResult::OK) {
            this->fiber->vm.Push(out.get());

            continue;
        }

        // Push default
        this->fiber->vm.Push(defaults->objects[i + 1]);
    }

    return true;
}

bool ArgumentBinder::FinalizeKwargs() {
    if (this->call_mode_is_kwarg) {
        if (ENUMBITMASK_ISFALSE(this->func->shared->kind, FunctionKind::KWARGS)) {
            SetTypeError(this->fiber->isolate, TypeError::Reason::NO_KWARGS,ORSTRING_TO_CSTR(this->func->shared->name));

            return false;
        }

        this->fiber->vm.Push((OObject *) this->kwargs);

        return true;
    }

    if (this->func->shared->IsKWargs()) {
        const auto dict = DictNew(this->fiber->isolate);
        if (!dict)
            return false;

        this->fiber->vm.Push((OObject *) dict.get());
    }

    return true;
}

bool ArgumentBinder::FinalizeRestArgs() {
    if (this->call_mode_is_rest) {
        this->fiber->vm.Push((OObject *) this->rest);

        return true;
    }

    if (this->func->shared->IsVariadic()) {
        const auto list = ListNew(this->fiber->isolate, 0);
        if (!list)
            return false;

        this->fiber->vm.Push((OObject *) list.get());
    }

    return true;
}

bool ArgumentBinder::NormalizeMethod() {
    if (!this->func->shared->IsMethod()) {
        this->stack_args -= 1;

        return true;
    }

    // Check object is instance. The receiver is the first of the bound arguments.
    const auto receiver = *this->ArgsBase();
    const auto *type = GetTypeInfoFromObject(this->fiber->isolate, receiver);

    if (O_IS_OBJECT(receiver) && !O_GET_RC(receiver).IsInstance())
        type = O_GET_TYPE(type);

    if (!IsTypeExtends(type, this->func->shared->owner_type)) {
        SetTypeErrorWithObj(this->fiber->isolate, TypeError::Reason::METHOD_RECEIVER, receiver);

        return false;
    }

    return true;
}

void ArgumentBinder::LoadCurrying() {
    const auto args = this->ArgsBase();

    const auto offset = this->func->currying->length;

    for (auto i = (int) this->stack_args - 1; i >= 0; i--)
        args[offset + i] = args[i];

    for (auto i = 0; i < this->func->currying->length; i++)
        args[i] = this->func->currying->objects[i];

    this->stack_args += this->func->currying->length;
    this->fiber->vm.regs.SP.reg += (offset * sizeof(void *));
}

CallResult ArgumentBinder::Bind(Fiber *fiber, Function *&func, const U16 p_count, const CallMode mode) {
    auto *callee = ResolveCallable(fiber->isolate, func);
    if (callee == nullptr)
        return CallResult::ERROR;

    func = callee;

    this->fiber = fiber;
    this->func = callee;
    this->nargs = (Dict *) fiber->vm.regs.r10.reg;
    this->rest = (List *) fiber->vm.regs.r11.reg;
    this->kwargs = (Dict *) fiber->vm.regs.r12.reg;
    this->stack_args = p_count;
    this->call_mode_is_kwarg = ENUMBITMASK_ISTRUE(mode, CallMode::KW_ARG);
    this->call_mode_is_nargs = ENUMBITMASK_ISTRUE(mode, CallMode::NARGS);
    this->call_mode_is_rest = ENUMBITMASK_ISTRUE(mode, CallMode::REST_ARG);

    if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD) && !this->NormalizeMethod())
        return CallResult::ERROR;

    if (!this->EnsureStack()) {
        ErrorSet(fiber->isolate,
                 MemoryError::Details[MemoryError::Reason::ID],
                 nullptr,
                 MemoryError::Details[MemoryError::Reason::STACK]);

        return CallResult::ERROR;
    }

    HList synthesized_rest;

    bool rest_edited = false;
    bool currying_pushed = false;

    auto total_args = this->stack_args;
    if (this->func->currying != nullptr)
        total_args += this->func->currying->length;

    if (total_args < this->func->shared->arity) {
        const auto args_diff = this->func->shared->arity - total_args;

        if (!this->call_mode_is_rest || this->rest->length < args_diff) {
            if (this->func->shared->IsInit()) {
                SetTypeError(fiber->isolate,
                             TypeError::Reason::INIT_NO_CURRY,
                             this->func->shared->owner_type->name);

                return CallResult::ERROR;
            }

            this->fiber->vm.regs.RR.reg = (PtrSize) FunctionNew(this->func, this->ArgsBase(), this->stack_args).get();
            this->fiber->vm.regs.SP.reg -= this->StackArgsBytes();

            return CallResult::DONE;
        }

        if (!this->func->shared->IsVariadic() && this->rest->length > args_diff) {
            SetTypeError(fiber->isolate,
                         TypeError::Reason::TOO_MANY_ARGS,
                         ORSTRING_TO_CSTR(this->func->shared->name),
                         (int) this->func->shared->arity,
                         (int) (this->stack_args + this->rest->length));

            return CallResult::ERROR;
        }

        if (this->func->currying != nullptr) {
            this->LoadCurrying();

            currying_pushed = true;
        }

        for (auto i = 0; i < args_diff; i++) {
            fiber->vm.Push(this->rest->objects[i]);

            this->stack_args += 1;
        }

        if (this->func->shared->IsVariadic()) {
            synthesized_rest = ListNew(fiber->isolate, this->rest->length - args_diff);
            if (!synthesized_rest)
                return CallResult::ERROR;

            ListExtend(synthesized_rest.get(), this->rest->objects + args_diff, this->rest->length - args_diff);

            this->rest = synthesized_rest.get();

            rest_edited = true;
        } else {
            // The list was fully consumed into the declared parameters (reaching
            // here means rest->length == args_diff: a shorter list took the
            // currying path above, a longer one already errored) and there is no
            // variadic parameter to receive a remainder. Clearing the flag stops
            // FinalizeRestArgs from pushing the spent list a second time, which
            // would both misbind the callee and leave SP off by one slot, since
            // that push is not counted in stack_args.
            this->call_mode_is_rest = false;
        }
    }

    if (!currying_pushed && this->func->currying != nullptr)
        this->LoadCurrying();

    if (this->call_mode_is_rest)
        total_args += this->rest->length;

    if (total_args > this->func->shared->arity) {
        if (!this->func->shared->IsVariadic()) {
            SetTypeError(fiber->isolate,
                         TypeError::Reason::TOO_MANY_ARGS,
                         ORSTRING_TO_CSTR(this->func->shared->name),
                         (int) this->func->shared->arity,
                         (int) total_args);

            return CallResult::ERROR;
        }

        if (!rest_edited) {
            const auto excess_on_stack = this->stack_args > this->func->shared->arity
                                             ? this->stack_args - this->func->shared->arity
                                             : 0;

            const auto rest_len = this->call_mode_is_rest ? this->rest->length : 0;

            synthesized_rest = ListNew(fiber->isolate, excess_on_stack + rest_len);
            if (!synthesized_rest)
                return CallResult::ERROR;

            if (excess_on_stack > 0) {
                ListExtend(synthesized_rest.get(), this->StackTop(excess_on_stack), excess_on_stack);

                this->fiber->vm.regs.SP.reg -= excess_on_stack * sizeof(void *);
                this->stack_args -= excess_on_stack;
            }

            if (this->call_mode_is_rest)
                ListExtend(synthesized_rest.get(), (OObject *) this->rest);

            this->rest = synthesized_rest.get();
        }

        this->call_mode_is_rest = true;
    }

    if (!this->ExpandDefaultArgs())
        return CallResult::ERROR;

    if (!this->FinalizeRestArgs())
        return CallResult::ERROR;

    if (!this->FinalizeKwargs())
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

    SetTypeErrorWithObj(isolate, TypeError::Reason::NON_CALLABLE, (OObject *) func);

    return nullptr;
}
