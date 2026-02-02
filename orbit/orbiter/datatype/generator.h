// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_GENERATOR_H_
#define ORBIT_ORBITER_DATATYPE_GENERATOR_H_

#include <atomic>

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    enum class GeneratorState : U8 {
        READY,
        RUNNING,
        EXHAUSTED
    };

    struct Generator {
        OROBJ_HEAD;

        Function *base;

        OObject **regs_dump;
        OObject **params;
        OObject **stack;

        PtrSize IP;

        std::atomic_uintptr_t acquired;
        std::atomic<GeneratorState> state;

        U32 stack_size;
    };

    using HGenerator = Handle<Generator>;

    /**
     * @brief Set up additional features and properties for the specified type
     *
     * This function enriches the previously created type with various functionalities.
     * It typically performs the following tasks:
     * - Adds default methods to the type
     * - Adds required properties to the type
     *
     * This function is called immediately after the type's Init function to complete its setup.
     *
     * @param self Pointer to TypeInfo created by %type%Init call
     *
     * @return true if setup was successful, false otherwise
     */
    bool GeneratorTypeSetup(TypeInfo *self);

    /**
     * @brief Creates a new generator object and initializes its state
     *
     * This function constructs a generator object, setting up the necessary memory allocations and initializing
     * its internal state. The generator is prepared to operate based on the provided fiber, base function,
     * and the specified parameter size.
     *
     * @param fiber Pointer to the Fiber object that provides the execution context for the generator
     * @param base Pointer to the Function object that defines the generator's base functionality
     * @param param_size The number of parameters to allocate for the generator's stack
     *
     * @return Handle to the newly created Generator object, or an empty handle if creation fails
     */
    HGenerator GeneratorNew(const Fiber *fiber, Function *base, U16 param_size);

    /**
     * @brief Initialize and create the specified type
     *
     * This function creates a new TypeInfo object representing the specific type.
     * It sets up the basic structure and core properties of the type.
     *
     * @param isolate Pointer to the Isolate in which the type is being created
     *
     * @return Handle to the newly created TypeInfo for the type, or an empty handle if creation failed
     */
    HOType GeneratorTypeInit(Isolate *isolate);
}

#endif // !ORBIT_ORBITER_DATATYPE_GENERATOR_H_
