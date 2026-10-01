// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_FRAME_H_
#define ORBIT_ORBITER_FRAME_H_

/**
 * @file
 * @brief The call frame layout, shared by the runtime and the compiler.
 *
 * This is ABI, not implementation: liftoff generates the code that runs inside
 * these frames and therefore has to know their shape, the same way it already
 * knows the instruction encoding.
 *
 * Only forward declarations are used below, so including this file costs
 * nothing: the frame holds pointers, and their size is all either side needs.
 */

namespace orbiter {
    namespace datatype {
        struct Context;
        struct Module;
        struct Code;
        struct OObject;
    } // namespace datatype

    /**
     * @brief The execution context saved at the bottom of a call frame's prologue.
     */
    struct FiberContext {
        datatype::Context *context;
        datatype::Module *module;
        datatype::Code *code;
        datatype::OObject *func;
    };

    /**
     * @brief Size of the frame prologue, in bytes.
     *
     * The prologue is written by Fiber::PushState at the stack pointer and reads,
     * from the bottom up: `Context | SP | BP | IP`. The SP word is the bottom of
     * the argument region the caller built, so returning through
     * Fiber::PopStateNoCtxRestore reclaims those arguments without anyone having
     * to count them.
     *
     * This is the unit the VM moves SP with.
     */
    constexpr auto kStackPrologueOffset = sizeof(FiberContext) + (sizeof(void *) * 3);

    static_assert(kStackPrologueOffset % sizeof(void *) == 0, "the frame prologue must be a whole number of slots");

    /**
     * @brief Size of the frame prologue, in stack slots.
     *
     * Both units are needed and they are not interchangeable. The VM moves SP by
     * the byte value; the bytecode encodes stack offsets in slots, because SKLDR
     * and SKSTR multiply their immediate by sizeof(void *). Codegen addresses
     * parameters relative to BP and therefore wants this one.
     *
     * Deriving it here, rather than writing the count out again on the compiler
     * side, is what keeps the two from drifting when the prologue changes.
     */
    constexpr auto kStackPrologueSlots = kStackPrologueOffset / sizeof(void *);
} // namespace orbiter

#endif // !ORBIT_ORBITER_FRAME_H_
