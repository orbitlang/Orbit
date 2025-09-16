// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_TRYCATCH_H_
#define ORBIT_ORBITER_TRYCATCH_H_

#include <orbit/orbiter/fiber.h>

namespace orbiter {
    class TryCatch {
        Fiber *fiber = nullptr;

        Panic **prev_ = nullptr;
        Panic *caught_error = nullptr;

    public:
        explicit TryCatch(Fiber *fiber) {
            this->fiber = fiber;

            if (fiber != nullptr) {
                this->prev_ = fiber->panic.r_current_;
                fiber->panic.r_current_ = &this->caught_error;
            }
        }

        TryCatch() noexcept : TryCatch(Fiber::Current()) {
        }

        [[nodiscard]] bool HasError() const noexcept {
            return this->caught_error != nullptr;
        }

        [[nodiscard]] datatype::OObject *GetError() const noexcept {
            return O_INCREF(this->caught_error->error);
        }

        ~TryCatch() {
            if (this->fiber != nullptr) {
                this->fiber->DiscardPanic();

                this->fiber->panic.r_current_ = this->prev_;
            }
        }
    };
}

#endif // !ORBIT_ORBITER_TRYCATCH_H_
