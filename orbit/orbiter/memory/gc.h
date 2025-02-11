// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MEMORY_GC_H_
#define ORBIT_ORBITER_MEMORY_GC_H_

#include <atomic>

#include <orbit/orbiter/datatype/obase.h>

#include <orbit/orbiter/memory/iallocator.h>
#include <orbit/orbiter/memory/bitoffset.h>

#define GC_GET_HEAD(ptr) ((GCHead *) (((unsigned char *) ptr) - sizeof(GCHead)))
#define GC_GET_OBJ(head) ((datatype::OObject*) (((unsigned char*) head) + sizeof(GCHead)))

namespace orbiter::memory {
    constexpr unsigned short kGCGenerations = 3;

    class alignas(ORBIT_ORBITER_MEMORY_QUANTUM) GCHead {
    public:
        GCHead *next = nullptr;
        GCHead **prev = nullptr;

        MSize r_count = 0;
        MSize size = 0;

        [[nodiscard]] bool IsTracked() const {
            return this->prev != nullptr;
        }

        [[nodiscard]] GCHead *Next() const {
            return (GCHead *) (((uintptr_t) this->next) & GCBitOffsets::AddressMask);
        }

        void SetNext(GCHead *head) {
            this->next = (GCHead *) (((uintptr_t) head) | ((uintptr_t) this->next & ~GCBitOffsets::AddressMask));
        }
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
        std::mutex track_lock;
        std::mutex rel_lock;

        GCGeneration generations[kGCGenerations]{};

        GCHead *rel_list = nullptr;

        MSize rel_count = 0;
        MSize rel_bytes = 0;
        MSize total_tracked = 0;

        std::atomic_uintptr_t allocated_bytes = 0;
    };

    class GC {
        IsolateAllocator allocator_;

        GCContext context_;

        explicit GC(Isolate *isolate) noexcept : allocator_(isolate) {
        }

        /**
         * @brief Executes the reference-count based garbage collection process by traversing the release list.
         *
         * It identifies and collects objects with a reference count of zero or removes actively used objects
         * (with a strong reference count greater than zero) from the release list to prevent erroneous collection.
         *
         * @return The number of objects that were successfully collected during the process.
         */
        MSize CollectRCQueue() noexcept;

        void Free(GCHead *head) noexcept;

        static void HeadInsert(GCHead **list, GCHead *head) noexcept;

        static void HeadRemove(const GCHead *head) noexcept;

        friend Isolate;

    public:
        GC() noexcept: allocator_(nullptr) {
        }

        datatype::OObject *AllocObject(MSize size) noexcept;

        void Free(datatype::OObject *object) noexcept {
            this->Free(GC_GET_HEAD(object));
        }

        void MarkForCollection(datatype::OObject *object) noexcept;

        void ThresholdCollect() noexcept;

        void Track(datatype::OObject *object) noexcept;
    };
}

#endif // !ORBIT_ORBITER_MEMORY_GC_H_
