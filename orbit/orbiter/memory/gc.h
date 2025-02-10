// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MEMORY_GC_H_
#define ORBIT_ORBITER_MEMORY_GC_H_

#include <atomic>

#include <orbit/orbiter/memory/iallocator.h>

namespace orbiter::memory {
    constexpr unsigned short kGCGenerations = 3;

    struct alignas(ORBIT_ORBITER_MEMORY_QUANTUM) GCHead {
        GCHead *next;
        GCHead **prev;

        MSize r_count;
        MSize size;
    };

    struct GCGeneration {
        GCHead *list;

        MSize count;
        MSize collected;
        MSize uncollected;

        U32 threshold;
        U32 times;
    };

    class GCContext {
    public:
        GCGeneration generations[kGCGenerations];

        std::atomic_uintptr_t allocated_bytes;
    };

    class GC {
        IsolateAllocator allocator_;

        GCContext *context_ = nullptr;

        explicit GC(Isolate *isolate) noexcept : allocator_(isolate) {
        }

        bool Initialize() noexcept;

        friend Isolate;

    public:
        GC() noexcept: allocator_(nullptr) {
        }

        void *AllocObject(MSize size) noexcept;

        void Free(void *ptr) const noexcept;
    };
}

#endif // !ORBIT_ORBITER_MEMORY_GC_H_
