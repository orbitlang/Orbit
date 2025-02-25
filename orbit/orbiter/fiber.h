// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_FIBER_H_
#define ORBIT_ORBITER_FIBER_H_

#include <orbit/orbiter/datatype/context.h>
#include <orbit/orbiter/datatype/module.h>

#include <orbit/orbiter/vm.h>

namespace orbiter {
    struct FiberError {
        datatype::OObject *value_;
        datatype::OObject **r_value_;
    };

    struct FiberContext {
        datatype::Context *context;
        datatype::Module *module;
        datatype::Code *code;
    };

    struct Fiber {
        VMContext vm;

        FiberError error;

        Isolate *isolate;

        FiberContext context;

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

        /**
         * @brief Restores the previously saved state of the Fiber from its stack.
         *
         * Copies the Fiber's context and general-purpose registers from the stack back into
         * the respective fields. Validates the size of the data being restored to match the
         * expected layout for a Fiber's context and registers. Decreases reference counts
         * for object values referenced by restored registers, releasing resources as needed.
         */
        void PopState() noexcept;

        static void SetCurrent(Fiber *fiber) noexcept;
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_FIBER_H_
