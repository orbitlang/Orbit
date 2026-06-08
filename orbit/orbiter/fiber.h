// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_FIBER_H_
#define ORBIT_ORBITER_FIBER_H_

#include <cassert>

#include <orbit/orbiter/datatype/context.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/module.h>
#include <orbit/orbiter/datatype/rguard.h>

#include <orbit/orbiter/import/module_entry.h>

#include <orbit/orbiter/defer.h>
#include <orbit/orbiter/panic.h>
#include <orbit/orbiter/vm.h>

namespace orbiter {
    struct FiberContext {
        datatype::Context *context;
        datatype::Module *module;
        datatype::Code *code;
        datatype::OObject *func;
    };

    constexpr auto kStackPrologueOffset = sizeof(FiberContext) + (sizeof(void *) * 2);
    constexpr auto kPreemptTick = 32;

    enum class FiberState : U8 {
        RUNNABLE, // ready, never executed or resumed after yield
        RUNNING, // currently executing (set by eval() on entry)
        YIELDED, // cooperatively yielded or preempted (re-enqueue)
        SUSPENDED, // waiting for event (not re-enqueued)
        COMPLETED, // terminated normally
        PANICKED // unhandled panic propagated to top
    };

    class Fiber {
    public:
        VMContext vm{};

        FiberContext context{};

        PanicContainer panic{};

        DeferStack defer_stack{};

        datatype::RGuard rguard{};

        Isolate *isolate = nullptr;

        Panic *panic_cache = nullptr;

        datatype::OObject *future = nullptr;

        import::ModuleEntry *module_entry = nullptr;

        struct {
            Fiber *next;
            Fiber **prev;
        } gc_set{};

        struct {
            Fiber *next;
            Fiber *prev;
        } queue{};

        void *active_ost = nullptr;

        FiberState state = FiberState::RUNNABLE;

        ~Fiber();

        /**
         * @brief Checks if the current fiber is in a panic state.
         *
         * @return True if the fiber is panicking; false otherwise.
         */
        [[nodiscard]] bool IsPanicking() const noexcept {
            return this->panic.HasPanic();
        }

        /**
         * @brief Saves the current fiber's execution state into its stack.
         *
         * This method pushes the fiber's context and key register states
         * (base pointer, instruction pointer (next instr)) onto its stack, preparing it
         * for future restoration or continuation of its execution.
         *
         * @return True if the state was successfully saved; false if there was
         *         insufficient stack space or another error occurred.
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
         * Create a new Fiber instance and initialize its context with the given stack size and stack limit.
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
         * @brief Retrieves and discards the current panic state of the Fiber.
         *
         * This method checks if there is an active panic in the current Fiber. If present, it retrieves
         * the associated error object, discards the panic, and returns the error object. If there is no
         * active panic, it returns an empty handle.
         *
         * @return A handle to the error object associated with the current panic if one exists,
         * otherwise an empty handle.
         */
        datatype::HOObject GetDiscardPanic() noexcept;

        /**
         * @brief Retrieves the current panic error object associated with the fiber, if one exists.
         *
         * This method checks if the fiber's internal panic state is set and, if so, retrieves the
         * associated error object encapsulated in an `HOObject`. If no panic state exists, an
         * empty `HOObject` is returned.
         *
         * @return An `HOObject` instance representing the panic error if present, or an empty
         *         `HOObject` if no panic state is set.
         */
        [[nodiscard]] datatype::HOObject GetPanicError() const noexcept;

        /**
         * @brief Deletes the provided Fiber instance, releasing associated resources.
         *
         * Behavior is undefined if an invalid Fiber instance is passed.
         *
         * @param fiber A pointer to the Fiber instance to be deleted.
         */
        static void Delete(Fiber *fiber) noexcept;

        /**
         * @brief Clears the panic handler chain for the current Fiber.
         *
         * Iterates through and safely discards all panic handlers in the Fiber's error chain.
         * Any out-of-memory errors are handled specially by re-linking them to an out-of-memory error
         * cache. Other errors are dereferenced and moved to the general panic cache.
         *
         */
        void DiscardPanic() noexcept;

        /**
         * @brief Handles a fiber-level exception by recording the provided error object.
         *
         * When a fiber encounters an exceptional state, this method captures the error
         * object and pushes it onto the fiber's panic chain. If no pre-allocated panic
         * structures are available, memory allocation is performed using the fiber's
         * isolate allocator. The error object is safely retained by incrementing its
         * reference count.
         *
         * @param error The error object representing the exceptional state to be recorded.
         */
        void Panic(datatype::OObject *error) noexcept;

        /**
         * @brief Restores the fiber's execution state from its stack.
         *
         * This method retrieves the previously saved fiber context and register states,
         * including the base pointer (BP) and instruction pointer (IP), from the fiber's
         * stack. It prepares the fiber to resume execution from the point where it was
         * previously suspended.
         *
         * This operation modifies the stack pointer (SP) to reflect the restored state
         * and ensures the integrity of the fiber's execution environment.
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

            this->panic.Reset();

            this->context.context = nullptr;
            this->context.module = nullptr;
            this->context.code = nullptr;
            this->context.func = nullptr;

            this->future = nullptr;
            this->module_entry = nullptr;

            this->vm.preempt_tick = kPreemptTick;

            this->state = FiberState::RUNNABLE;
        }

        /**
         * @brief Sets the execution context for the current Fiber instance.
         *
         * Updates the context associated with the Fiber by assigning the provided Context,
         * Module, and Code instances to the Fiber's internal state. Resets the function pointer
         * to null and initializes the instruction pointer (IP) register to point to the start
         * of the provided Code instance.
         *
         * @param context A pointer to the Context object representing the execution state.
         * @param module A pointer to the Module object associated with the execution.
         * @param code A pointer to the Code object containing the executable code. Must not be null.
         */
        void SetContext(datatype::Context *context, datatype::Module *module, datatype::Code *code) noexcept {
            assert(code != nullptr);

            this->context.context = context;
            this->context.module = module;
            this->context.code = code;
            this->context.func = nullptr;

            this->vm.regs.IP.reg = (PtrSize) code->m_code;
        }

        /**
         * @brief Sets the execution context for the current Fiber instance.
         *
         * Updates the Fiber's context to align with the provided function. This includes
         * setting the internal context, associated module, and code. Additionally, assigns
         * the function to the context's function pointer for further execution tracking.
         *
         * @param func A pointer to the Function object containing context, module, and code
         *             information to be applied to the Fiber.
         */
        void SetContext(datatype::Function *func) noexcept {
            this->SetContext(func->shared->context, func->shared->module, func->shared->code);

            this->context.func = (datatype::OObject *) func;
        }

        /**
         * @brief Sets the current thread-local Fiber instance.
         *
         * Updates the thread-local variable to point to the specified Fiber instance. This
         * establishes the given Fiber as the currently active Fiber for the executing thread.
         *
         * @param fiber A pointer to the Fiber instance to set as the current thread-local instance.
         *              Can be nullptr to clear the current Fiber instance.
         */
        static void SetCurrent(Fiber *fiber) noexcept;
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_FIBER_H_
