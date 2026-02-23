// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_VMEXCSTACK_H_
#define ORBIT_ORBITER_VMEXCSTACK_H_

#include <orbit/orbiter/isolate.h>

namespace orbiter {
    struct ExceptionContext {
        PtrSize _sentinel_;

        ExceptionContext *prev;

        struct {
            U32 ret_pops: 30;
            U32 action: 2;
        };

        U32 coffset;
        U32 foffset;

        PtrSize key;
        PtrSize ret_value;
    };

    constexpr auto kExceptionStackSize = 16 * sizeof(ExceptionContext);
    constexpr auto kExceptionStackMinSize = 2 * sizeof(ExceptionContext);
    constexpr PtrSize kExceptionContextTag = (0xECECEC << 1) | 0x1;

    struct VMExcStack {
        ExceptionContext *stack;

        U16 count;
        U16 limit;

        /**
         * @brief Initializes the exception stack for a VM instance.
         *
         * This method sets up the exception stack for a VM instance using the specified
         * isolate and exception count. It allocates the necessary memory and configures
         * the internal stack structure.
         *
         * @param isolate A pointer to the Isolate associated with the current VM instance,
         *                used for memory allocation.
         * @param exceptions The maximum number of exceptions that the stack can manage.
         *                   Must be less than the defined minimum exception stack size.
         * @return True if the initialization was successful, false otherwise.
         */
        bool Init(Isolate *isolate, U16 exceptions) noexcept;

        /**
         * @brief Initializes the exception stack for a VM instance using a default size.
         *
         * This method initializes the exception stack using the specified isolate
         * and a default exception stack size defined by kExceptionStackSize.
         *
         * @param isolate A pointer to the Isolate associated with the current VM instance,
         *                used for memory allocation.
         * @return True if the initialization was successful, false otherwise.
         */
        bool Init(Isolate *isolate) noexcept {
            return this->Init(isolate, kExceptionStackSize);
        }

        /**
         * @brief Pushes a new exception context onto the stack.
         *
         * This method creates a new exception context, links it to the previous
         * exception context provided, and sets its catch offset. This is primarily
         * used for managing the exception handling flow within a virtual machine.
         *
         * @param prev A pointer to the previous ExceptionContext representing
         *             the current top of the stack.
         * @param coffset The offset representing the location to catch
         *                     exceptions at runtime.
         * @return A pointer to the newly created ExceptionContext, or nullptr
         *         if the operation fails.
         */
        ExceptionContext *Push(ExceptionContext *prev, U32 coffset) noexcept;

        /**
         * @brief Cleans up the allocated memory used by the ExceptionStack.
         *
         * This method releases the memory previously allocated for the ExceptionStack.
         *
         * @param isolate A pointer to the Isolate associated with the ExceptionStack instance for memory deallocation.
         */
        void Cleanup(Isolate *isolate) const;

        /**
         * @brief Removes the most recent exception context from the stack.
         *
         * This method decreases the count of stored exception contexts on the stack,
         * effectively discarding the topmost context. It is a no-operation if there
         * are no exception contexts to remove.
         */
        void Pop() noexcept;
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_VMEXCSTACK_H_
