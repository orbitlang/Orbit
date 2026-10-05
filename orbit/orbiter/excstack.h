// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_VMEXCSTACK_H_
#define ORBIT_ORBITER_VMEXCSTACK_H_

#include <cassert>

#include <orbit/orbiter/isolate.h>

namespace orbiter {
    constexpr PtrSize kExceptionContextTag = (0xECECEC << 2) | 0x1;

    struct ExceptionContext;

    struct ExPtrContainer {
        PtrSize ptr{};

        ExPtrContainer() = default;

        [[nodiscard]] ExceptionContext *prev() const {
            return (ExceptionContext *) (this->ptr & ~0x3);
        }

        [[nodiscard]] U32 action() const {
            return this->ptr & 0x3;
        }

        void SetAction(const U32 action) {
            this->ptr = (this->ptr & ~(PtrSize) 0x3) | (action & 0x3);
        }
    };

    struct ExceptionContext {
        PtrSize _sentinel_;

        ExPtrContainer info;

        U32 catch_off;
        U32 finally_off;

        PtrSize key;

        PtrSize jump_target;
        PtrSize ret_value;

        ExceptionContext(ExceptionContext *prev, const PtrSize key, const U32 catch_off)
            : _sentinel_(kExceptionContextTag),
              catch_off(catch_off),
              finally_off(0),
              key(key),
              jump_target(0),
              ret_value(0) {
            // The action travels in the low bits of the same word, so the previous
            // context has to leave them free.
            assert(((PtrSize) prev & 0x3) == 0);

            this->info.ptr = (PtrSize) prev;
        }
    };

    static_assert(std::is_standard_layout_v<ExPtrContainer>);
    static_assert(std::is_standard_layout_v<ExceptionContext>);

    // The context is carved out of the VM stack, and codegen reserves room for it
    // as `sizeof(ExceptionContext) / sizeof(PtrSize)` slots: a size that is not a
    // whole number of slots would silently reserve one too few.
    static_assert(sizeof(ExceptionContext) % sizeof(PtrSize) == 0,
                  "the exception context must be a whole number of stack slots");
} // namespace orbiter

#endif // !ORBIT_ORBITER_VMEXCSTACK_H_
