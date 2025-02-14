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

    constexpr unsigned int kGCMinHeapSize = 1 * kToMBytes;
    constexpr unsigned int kGCMaxHeapSize = 256 * kToMBytes;

    constexpr unsigned int kGCThresholdElementsCount = 10000;
    constexpr unsigned int kGCRCThresholdElementsCount = 2000;

    class alignas(ORBIT_ORBITER_MEMORY_QUANTUM) GCHead {
    public:
        GCHead *next = nullptr;
        GCHead **prev = nullptr;

        MSize r_count = 0;
        MSize size = 0;

        [[nodiscard]] bool IsTracked() const {
            return this->prev != nullptr;
        }

        [[nodiscard]] bool IsFinalized() const {
            return ((((uintptr_t) this->next) & GCBitOffsets::FinalizedMask) >> GCBitOffsets::FinalizedShift);
        }

        [[nodiscard]] bool IsVisited() const {
            return ((((uintptr_t) this->next) & GCBitOffsets::VisitedMask) >> GCBitOffsets::VisitedShift);
        }

        [[nodiscard]] GCHead *Next() const {
            return (GCHead *) (((uintptr_t) this->next) & GCBitOffsets::AddressMask);
        }

        void SetNext(GCHead *head) {
            this->next = (GCHead *) (((uintptr_t) head) | ((uintptr_t) this->next & ~GCBitOffsets::AddressMask));
        }

        void SetFinalize(bool visited) {
            if (visited)
                this->next = (GCHead *) (((uintptr_t) this->next) | GCBitOffsets::FinalizedMask);
            else
                this->next = (GCHead *) (((uintptr_t) this->next) & ~GCBitOffsets::FinalizedMask);
        }

        void SetVisited(bool visited) {
            if (visited)
                this->next = (GCHead *) (((uintptr_t) this->next) | GCBitOffsets::VisitedMask);
            else
                this->next = (GCHead *) (((uintptr_t) this->next) & ~GCBitOffsets::VisitedMask);
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

        MSize tracked_count = 0;
        MSize tracked_bytes = 0;

        std::atomic_uintptr_t allocated_bytes = 0;
    };

    class GC {
        IsolateAllocator allocator_;

        GCContext context_;

        std::atomic_bool enabled_;

        MSize max_heap_size_;

        explicit GC(Isolate *isolate, U32 heap_size) noexcept : allocator_(isolate), enabled_(true),
                                                                max_heap_size_(heap_size) {
            if (heap_size < kGCMinHeapSize)
                this->max_heap_size_ = kGCMinHeapSize;
        }

        explicit GC(Isolate *isolate) noexcept : GC(isolate, kGCMaxHeapSize) {
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

        void ResetStats(int generation) noexcept;

        static void Trace(datatype::OObject *object, bool inc) noexcept;

        static void TraceRoots(GCGeneration *generation, GCHead **unreachable) noexcept;

        void Trashing(GCGeneration *nextgen, GCHead *unreachable) noexcept;

        static void SearchRoots(const GCGeneration *generation) noexcept;

        friend Isolate;

    public:
        MSize Collect() noexcept;

        MSize Collect(int generation) noexcept;

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
