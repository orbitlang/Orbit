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

        [[nodiscard]] U32 prev() const {
            return this->ptr >> 2;
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

        ExceptionContext(const U32 offset_prev, const PtrSize key, const U32 catch_off)
            : _sentinel_(kExceptionContextTag),
              catch_off(catch_off),
              finally_off(0),
              key(key),
              jump_target(0),
              ret_value(0) {
            // `offset_prev` is the CP value of the enclosing context (0 for none).
            // It is shifted up by two bits so the pending action can travel in the
            // low bits of the same word; prev() shifts it back down.
            this->info.ptr = offset_prev << 2;
        }

        U32 GetOffset(const void *base) const {
            // Biased by one: CP == 0 means no context, and a context can sit at
            // offset 0 (the first slot of the module frame, whose BP is 0).
            return ((unsigned char *) this - (unsigned char *) base) + 1;
        }
    };

    static_assert(std::is_standard_layout_v<ExPtrContainer>);
    static_assert(std::is_standard_layout_v<ExceptionContext>);

    // The context is carved out of the VM stack, and codegen reserves room for it
    // as `sizeof(ExceptionContext) / sizeof(PtrSize)` slots: a size that is not a
    // whole number of slots would silently reserve one too few.
    static_assert(sizeof(ExceptionContext) % sizeof(PtrSize) == 0,
                  "the exception context must be a whole number of stack slots");

    inline ExceptionContext *ExContextGet(const void *base, const U32 offset) {
        if (offset == 0)
            return nullptr;

        return (ExceptionContext *) ((unsigned char *) base + (offset - 1));
    }
} // namespace orbiter

#endif // !ORBIT_ORBITER_VMEXCSTACK_H_
