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

    struct Fiber {
        VMContext vm;

        FiberError error;

        struct {
            datatype::Context *context;
            datatype::Module *module;
            datatype::Code *code;
        } context;

        struct {
            Fiber *next;
            Fiber **prev;
        } gc_set;

        /**
         * Returns the current thread-local Fiber instance.
         *
         * @return A pointer to the current Fiber instance if one exists, otherwise nullptr.
         */
        static Fiber *Current() noexcept;

        static void SetCurrent(Fiber *fiber) noexcept;

        /**
         * Creates a new Fiber instance associated with the provided Isolate and initializes
         * its context with the given stack size.
         *
         * @param isolate A pointer to the Isolate to which the new Fiber will belong.
         * @param stackSize The size of the stack to allocate for the Fiber.
         * @return A pointer to the newly created Fiber instance. Returns nullptr if the Fiber
         *         could not be created or initialized successfully.
         */
        static Fiber *New(Isolate *isolate, MSize stackSize) noexcept;
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_FIBER_H_
