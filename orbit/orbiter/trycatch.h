// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_TRYCATCH_H_
#define ORBIT_ORBITER_TRYCATCH_H_

#include <orbit/orbiter/fiber.h>

namespace orbiter {
    class TryCatch {
        PanicContainer *container_ = nullptr;
        Panic **panic_cache_ = nullptr;

        Panic **prev_ = nullptr;
        Panic *caught_error = nullptr;

    public:
        explicit TryCatch(Isolate *isolate) {
            this->container_ = &isolate->panic;
            this->panic_cache_ = &isolate->panic_cache;

            this->prev_ = this->container_->r_current_;
            this->container_->r_current_ = &this->caught_error;
        }

        explicit TryCatch(Fiber *fiber) {
            this->container_ = &fiber->panic;
            this->panic_cache_ = &fiber->panic_cache;

            this->prev_ = this->container_->r_current_;
            this->container_->r_current_ = &this->caught_error;
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
            if (this->container_ != nullptr) {
                this->container_->DiscardPanic(this->panic_cache_);

                this->container_->r_current_ = this->prev_;
            }
        }
    };
}

#endif // !ORBIT_ORBITER_TRYCATCH_H_
