// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_RGUARD_H_
#define ORBIT_ORBITER_DATATYPE_RGUARD_H_

#include <orbit/orbiter/datatype/oobject.h>

namespace orbiter::datatype {
    constexpr auto kRGuardInitialCapacity = 8;

    class RGuard {
        OObject **stack_ = nullptr;

        MSize capacity_ = 0;
        MSize length_ = 0;

        bool Push(Isolate *isolate, OObject *object);

        bool Reserve(Isolate *isolate, MSize size);

        void Cleanup(Isolate *isolate);

        void Pop();

        friend class orbiter::Fiber;
        friend class ReprGuard;
    };

    class ReprGuard {
        bool cyclic_ = false;
        bool error_ = false;

    public:
        explicit ReprGuard(OObject *self);

        ~ReprGuard();

        [[nodiscard]] bool IsCyclic() const noexcept { return cyclic_; }

        [[nodiscard]] bool IsError() const noexcept { return error_; }
    };
}

#endif // !ORBIT_ORBITER_DATATYPE_RGUARD_H_
