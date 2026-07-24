// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_ORBCALL_H_
#define ORBIT_ORBITER_ORBCALL_H_

#include <orbit/orbiter/datatype/dict.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/list.h>

#include <orbit/orbiter/fiber.h>
#include <orbit/orbiter/vm.h>

namespace orbiter {
    class ArgumentBinder {
        Fiber *fiber = nullptr;

        const datatype::Function *func = nullptr;

        datatype::Dict *nargs = nullptr;
        datatype::List *rest = nullptr;
        datatype::Dict *kwargs = nullptr;

        U16 stack_args = 0;

        bool call_mode_is_kwarg = false;
        bool call_mode_is_nargs = false;
        bool call_mode_is_rest = false;

        /**
         * @brief Retrieves a pointer to the top of the VM stack, adjusted by the specified number of slots.
         *
         * This method calculates the address of the stack top by taking into account the current
         * stack pointer position and the number of requested slots. The resulting pointer points
         * to the target location within the stack.
         *
         * @param slots The number of slots to adjust from the top of the stack. Each slot is
         *              presumed to be the size of a pointer.
         *
         * @return A double pointer to the datatype::OObject located at the specified position on the stack.
         *         This pointer is adjusted by the given number of slots.
         */
        [[nodiscard]] datatype::OObject **StackTop(const MSize slots) const noexcept {
            return (datatype::OObject **) ((this->fiber->vm.stack.stack + this->fiber->vm.regs.SP.reg) - (slots * sizeof(void *)));
        }

        /**
         * @brief Retrieves the base pointer of the argument region on the VM stack.
         *
         * This method calculates and returns the starting point for the arguments
         * laid out on the stack. It internally uses the stack top position adjusted
         * by the number of argument slots.
         *
         * @return A double pointer to the datatype::OObject representing the base of
         *         the argument region on the stack.
         */
        [[nodiscard]] datatype::OObject **ArgsBase() const noexcept {
            return this->StackTop(this->stack_args);
        }

        bool EnsureStack() const;

        bool ExpandDefaultArgs();

        bool FinalizeKwargs();

        bool FinalizeRestArgs();

        bool NormalizeMethod();

        void LoadCurrying();

    public:
        /**
         * @brief Bind the call's arguments to the callee's parameters, laying them
         * out on the VM stack.
         *
         * Note this builds only the argument region: the call frame proper (BP,
         * prologue) is set up later by the caller.
         *
         * Resolves the callee, normalizes a method receiver, applies currying,
         * fills from the rest list, expands defaults and finalizes rest/kwargs.
         *
         * @param fiber   Fiber owning the stack the arguments are laid out on.
         * @param func    Value being called; on success it is replaced by the
         *                function that will actually run (a type's constructor).
         * @param p_count Number of arguments already pushed by the caller.
         * @param mode    Call modifiers (method, named/kw/rest arguments).
         *
         * @return CallResult::OK when the arguments are laid out — read the resulting
         *         argument count with StackArgs();
         *         CallResult::DONE when currying already satisfied the call and
         *         nothing has to be invoked;
         *         CallResult::ERROR with the error set on the fiber.
         */
        datatype::CallResult Bind(Fiber *fiber, datatype::Function *&func, U16 p_count, CallMode mode);

        /**
         * @brief Calculates the total size, in bytes, of the argument region on the stack.
         *
         * This method computes the memory footprint of the arguments currently laid out on
         * the stack. The size is determined using the number of argument slots and the size
         * of a pointer.
         *
         * @return The size, in bytes, of the arguments on the stack as an MSize value.
         */
        [[nodiscard]] MSize StackArgsBytes() const noexcept {
            return this->stack_args * sizeof(void *);
        }

        /**
         * @brief Retrieves the number of argument slots bound for the current call.
         *
         * This method provides access to the count of arguments that have been laid out
         * on the stack. The returned value can be used to determine how many
         * arguments are involved in the current invocation.
         *
         * @return The number of argument slots as an unsigned 16-bit integer.
         */
        [[nodiscard]] U16 StackArgs() const noexcept {
            return this->stack_args;
        }
    };

    /**
     * @brief Resolve a call target to the function that will actually run.
     *
     * A Function is returned as-is; a type object resolves to its constructor.
     *
     * @param isolate Isolate used to raise the error.
     * @param func    Value being called.
     *
     * @return The function to invoke, or nullptr with a TypeError raised when
     * the value is not callable.
     */
    datatype::Function *ResolveCallable(Isolate *isolate, datatype::Function *func);
}

#endif // !ORBIT_ORBITER_ORBCALL_H_
