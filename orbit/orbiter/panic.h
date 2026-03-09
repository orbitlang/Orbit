// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_PANIC_H_
#define ORBIT_ORBITER_PANIC_H_

#include <orbit/orbiter/datatype/obase.h>

namespace orbiter {
    struct Panic {
        Panic *prev;

        datatype::OObject *error;

        PtrSize frame;

        [[nodiscard]] bool ISAborted() const {
            return this->prev != nullptr;
        }
    };

    class PanicContainer {
    public:
        Panic **r_current_ = nullptr;
        Panic *current_ = nullptr;

        Panic *CreatePanic(Isolate *isolate, Panic **panic_cache, datatype::OObject *error) const noexcept;

        Panic *CreateOOMPanic(const Isolate *isolate, Panic **panic_cache) const;

        void DiscardPanic(Panic **panic_cache) const noexcept;

        void Reset() noexcept;
    };
} // namespace orbiter

#endif // !ORBIT_ORBITER_PANIC_H_
