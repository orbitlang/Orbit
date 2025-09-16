// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_FIBER_H_
#define ORBIT_ORBITER_FIBER_H_

#include <cassert>

#include <orbit/orbiter/datatype/context.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>

#include <orbit/orbiter/vm.h>

namespace orbiter {
    struct Panic {
        Panic *prev;

        datatype::OObject *error;

        [[nodiscard]] bool ISAborted() const {
            return this->prev != nullptr;
        }
    };

    class FiberPanic {
        Panic **r_current_;

        friend Fiber;
        friend class TryCatch;

    public:
        Panic *current_;
    };

    struct FiberContext {
        datatype::Context *context;
        datatype::Module *module;
        datatype::Code *code;
        datatype::Function *func;
    };

    struct Fiber {
        VMContext vm;

        FiberContext context;

        FiberPanic panic;

        Isolate *isolate;

        Panic *panic_cache;

        struct {
            Fiber *next;
            Fiber **prev;
        } gc_set;

        struct {
            Fiber *next;
            Fiber *prev;
        } queue;

        /**
         * @brief Saves the current state of the Fiber onto its stack.
         *
         * Pushes the current Fiber's context and general-purpose registers onto the stack,
         * ensuring enough stack space exists beforehand. Updates the stack pointer (SP) and base
         * pointer (BP) registers. Increments reference counts for object values referenced by
         * saved registers.
         *
         * @return True if the state was successfully saved onto the stack, otherwise false.
         */
        bool PushState() noexcept;

        /**
         * @brief Returns the current thread-local Fiber instance.
         *
         * @return A pointer to the current Fiber instance if one exists, otherwise nullptr.
         */
        static Fiber *Current() noexcept;

        /**
         * @brief Creates a new Fiber instance associated with the provided Isolate.
         *
         * Create a new Fiber instance and initializes its context with the given stack size and stack limit.
         *
         * @param isolate A pointer to the Isolate to which the new Fiber will belong.
         * @param stack_size The size of the stack to allocate for the Fiber.
         * @param stack_limit The limit of the stack size for the Fiber.
         *
         * @return A pointer to the newly created Fiber instance. Returns nullptr if the Fiber
         *         could not be created or initialized successfully.
         */
        static Fiber *New(Isolate *isolate, MSize stack_size, MSize stack_limit) noexcept;

        /**
         * @brief Deletes the provided Fiber instance, releasing associated resources.
         *
         * Behavior is undefined if an invalid Fiber instance is passed.
         *
         * @param fiber A pointer to the Fiber instance to be deleted.
         */
        static void Delete(Fiber *fiber) noexcept;

        void DiscardPanic() noexcept;

        void Panic(datatype::OObject *error) noexcept;

        /**
         * @brief Restores the previously saved state of the Fiber from its stack.
         *
         * Copies the Fiber's context and general-purpose registers from the stack back into
         * the respective fields. Validates the size of the data being restored to match the
         * expected layout for a Fiber's context and registers. Decreases reference counts
         * for object values referenced by restored registers, releasing resources as needed.
         */
        void PopState() noexcept;

        /**
         * @brief Resets the state of the current Fiber instance to its default values.
         *
         * Resets the Fiber's virtual machine (VM) registers by zeroing out their
         * memory to ensure no residual data remains. Updates the VM state to
         * VMState::RUNNABLE, marking the Fiber as ready to run. Clears any existing
         * error state by setting the error value pointer to null and updating the
         * reference pointer appropriately.
         */
        void Reset() noexcept {
            memory::MemoryZero(&this->vm.regs, sizeof(Registers));

            this->vm.state = VMState::RUNNABLE;

            this->panic.current_ = nullptr;
            this->panic.r_current_ = &this->panic.current_;
        }

        /**
         * @brief Updates the Fiber's execution context with the provided context, module, and code values.
         *
         * Associates the Fiber's internal context with the provided `Context`, `Module`,
         * and `Code` objects, incrementing their reference counts as needed to ensure
         * proper lifecycle management. Additionally, updates the instruction pointer
         * register (IP) to point to the code's memory location.
         *
         * @param context A pointer to the Context object to associate with the Fiber.
         * @param module A pointer to the Module object containing module-related data.
         * @param code A pointer to the Code object containing executable code.
         */
        void SetContext(datatype::Context *context, datatype::Module *module, datatype::Code *code) noexcept {
            assert(code != nullptr);

            this->context.context = O_FAST_INCREF(context);
            this->context.module = O_FAST_INCREF(module);
            this->context.code = O_FAST_INCREF(code);
            this->context.func = nullptr;

            this->vm.regs.IP.reg = (PtrSize) code->m_code;
        }

        static void SetCurrent(Fiber *fiber) noexcept;

        /**
         * @brief Releases the Fiber's current execution context and associated resources.
         *
         * Calls the `Release` function on the Fiber's internal `context`, `module`, and `code`
         * fields to properly decrement reference counts or free resources. Ensures the Fiber's
         * execution context is unset and its associated data is cleaned up.
         */
        void UnsetContext() noexcept {
            O_FAST_DECREF(this->context.context);
            O_FAST_DECREF(this->context.module);
            O_FAST_DECREF(this->context.code);

            this->context.context = nullptr;
            this->context.module = nullptr;
            this->context.code = nullptr;
        }
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_FIBER_H_
