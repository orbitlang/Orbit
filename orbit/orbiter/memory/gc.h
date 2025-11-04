// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_MEMORY_GC_H_
#define ORBIT_ORBITER_MEMORY_GC_H_

#include <atomic>

#include <orbit/util/macros.h>

#include <orbit/orbiter/datatype/obase.h>

#include <orbit/orbiter/memory/iallocator.h>
#include <orbit/orbiter/memory/bitoffset.h>

#define GC_GET_HEAD(ptr) ((GCHead *) (((unsigned char *) ptr) - sizeof(GCHead)))
#define GC_GET_OBJ(head) ((datatype::OObject*) (((unsigned char*) head) + sizeof(GCHead)))

namespace orbiter {
    class Fiber;

    namespace memory {
        constexpr unsigned short kGCGenerations = 3;

        constexpr unsigned int kGCMinHeapSize = 1 * kToMBytes;
        constexpr unsigned int kGCMaxHeapSize = 256 * kToMBytes;

        constexpr unsigned int kGCThresholdElementsCount = 10000;
        constexpr unsigned int kGCRCThresholdElementsCount = 2000;

        constexpr unsigned char kGCPromotionThresholdGen0 = 2;
        constexpr unsigned char kGCPromotionThresholdGen1 = 3;

#ifdef _ORBIT_ENVIRON_64BIT_
        constexpr unsigned char kGCMaxEpoch = 0xFFFFFFFFFFFFFE; // (1u<<56) - 2
#elif  _ORBIT_ENVIRON_32BIT_
        constexpr unsigned char kGCMaxEpoch = 0xFFFFFE;         // (1u<<24) - 2
#else
#error "invalid environment."
#endif

        /**
         * @brief Represents a node in the garbage collection (GC) management chain with metadata for object tracking.
         *
         * The GCHead class is utilized to manage and track objects within a garbage-collected memory system. Each instance
         * of this class contains information about the object's reference count, size, and its position within linked GC
         * structures. This class provides methods to determine the state of the tracked object, including whether it has
         * been visited, finalized, or is currently tracked. Additionally, it allows manipulation of the next tracked object
         * and state flags using bit-level operations.
         */
        class alignas(ORBIT_ORBITER_MEMORY_QUANTUM) GCHead {
        public:
            GCHead *next = nullptr;
            GCHead **prev = nullptr;

#ifdef _ORBIT_ENVIRON_64BIT_
            struct {
                MSize epoch: 56 = 0;
                MSize age: 8 = 0;
            };
#elif  _ORBIT_ENVIRON_32BIT_
            struct {
                MSize epoch: 24 = 0;
                MSize age: 8 = 0;
            };
#else
#error "invalid environment"
#endif

            MSize size = 0;

            [[nodiscard]] bool IsContainer() const {
                return ((((uintptr_t) this->next) & GCBitOffsets::ContainerTypeMask) >>
                        GCBitOffsets::ContainerTypeShift);
            }

            [[nodiscard]] bool IsFinalized() const {
                return ((((uintptr_t) this->next) & GCBitOffsets::FinalizedMask) >> GCBitOffsets::FinalizedShift);
            }

            [[nodiscard]] bool IsTracked() const {
                return this->prev != nullptr;
            }

            [[nodiscard]] bool IsVisited(MSize epoch) const {
                return this->epoch == epoch || this->epoch == (epoch - 1);
            }

            [[nodiscard]] GCHead *Next() const {
                return (GCHead *) (((uintptr_t) this->next) & GCBitOffsets::AddressMask);
            }

            void SetContainerType() {
                this->next = (GCHead *) ((uintptr_t) this->next | GCBitOffsets::ContainerTypeMask);
            }

            void SetFinalize(bool visited) {
                this->next = (GCHead *) (visited
                                             ? (uintptr_t) this->next | GCBitOffsets::FinalizedMask
                                             : (uintptr_t) this->next & ~GCBitOffsets::FinalizedMask);
            }

            void SetNext(GCHead *head) {
                this->next = (GCHead *) (((uintptr_t) head) | ((uintptr_t) this->next & ~GCBitOffsets::AddressMask));
            }

            void SetVisited(MSize epoch) {
                this->epoch = epoch;
            }
        };

        /**
         * @brief Represents a transient list for managing garbage collection (GC) objects in a temporary context.
         *
         * The GCTransientList class provides utility for managing a temporary collection of GCHead objects during
         * garbage collection operations. It allows operations such as merging its contents into another list and
         * adding new GC objects to the head of the list while maintaining proper pointers and metadata integrity.
         * This class ensures the tracked objects are organized in a chain with appropriate connection and disconnection
         * mechanisms.
         */
        class GCTransientList {
            MSize count = 0;

        public:
            GCHead *head = nullptr;
            GCHead *tail = nullptr;

            /**
             * @brief Merges the current list into a destination list, resetting the source list and returning the number of elements transferred.
             *
             * This method combines the contents of the current GCTransientList into the specified destination.
             * The source list (represented by this object) is cleared after transfer, and the number of elements moved
             * is returned. Updates are made to ensure proper linkage between the lists to maintain their integrity.
             *
             * @param destination A double pointer to the head of the destination list, where the elements from the current list
             *                    should be appended.
             * @return The number of elements transferred from the current list to the destination list.
             */
            MSize MergeTo(GCHead **destination) {
                const auto count = this->count;

                if (this->head == nullptr)
                    return 0;

                this->tail->SetNext(*destination);

                if (*destination != nullptr)
                    (*destination)->prev = &this->tail->next;

                *destination = this->head;
                this->head->prev = destination;

                this->head = nullptr;
                this->tail = nullptr;
                this->count = 0;

                return count;
            }

            /**
             * @brief Adds a GCHead node to the head of the GCTransientList.
             *
             * @param head A pointer to the GCHead object to be added to the head of the list.
             */
            void AddHead(GCHead *head) {
                if (this->head == nullptr) {
                    head->SetNext(nullptr);
                    head->prev = &this->head;
                } else {
                    head->SetNext(this->head);

                    if (this->head != nullptr)
                        this->head->prev = &head->next;

                    head->prev = &this->head;
                }

                this->head = head;

                if (this->tail == nullptr)
                    this->tail = head;

                this->count += 1;
            }
        };

        /**
         * @brief Represents a generation in a generational garbage collection system.
         *
         * The GCGeneration struct is used to manage and track objects within a specific generation in a garbage-collected memory system.
         * Generational garbage collection segregates objects based on their age, allowing optimizations for frequently collected younger objects
         * while reducing overhead for older, frequently referenced objects. Each generation maintains metadata about the objects under its control
         * and the collection process.
         */
        struct GCGeneration {
            GCHead *list;

            MSize count;

            MSize collected;
            MSize uncollected;

            U32 promotion_threshold;
            U32 threshold;
            U32 times;
        };

        /**
         * @brief Represents the context for managing garbage collection operations and related resources.
         *
         * The GCContext class provides critical synchronization mechanisms and metadata structures
         * required to manage garbage collection in a memory system. It encapsulates locks and
         * data structures for generational garbage collection, and object tracking.
         * It serves as the core structure for coordinating the various components involved in garbage
         * collection processes.
         *
         * Key features include:
         *
         * - Synchronization:
         *   Contains multiple mutex locks to ensure thread-safe operations across garbage collection tasks,
         *   such as garbage list updates, tracked object management and interactions with the virtual machine.
         *
         * - Generational Collection:
         *   Manages multiple generations for garbage collection using the `generations` array, enabling
         *   optimized collection of objects based on their lifespan and access patterns.
         *
         * - Object Tracking:
         *   Maintains metadata for garbage, including objects awaiting collection, reference-counted objects,
         *   and tracked allocations. Provides counters for the number of objects and their total memory usage
         *   in various states.
         */
        class GCContext {
        public:
            std::mutex garbage_lock;
            std::mutex track_lock;
            std::mutex run_lock;
            std::mutex vm_lock;

            GCGeneration generations[kGCGenerations]{};

            GCHead *garbage = nullptr;

            MSize tracked_count = 0;
            MSize tracked_bytes = 0;
        };

        /**
         * @brief Represents a garbage collector responsible for memory management and cleanup of unused objects.
         *
         * The GC class provides mechanisms to allocate, free, and manage memory in a controlled environment.
         * It employs various garbage collection strategies, such as reference counting, generational collection,
         * and threshold-based triggers. The garbage collector ensures the application's memory usage remains
         * within specified heap size limits while preventing memory leaks and over-allocation.
         */
        class GC {
            IsolateAllocator allocator_;

            GCContext context_;

            Fiber *fibers_;

            MSize epoch = 2;

            MSize max_heap_size_;

            std::atomic_bool enabled_ = true;

            std::atomic_uintptr_t allocated_bytes_ = 0;

            explicit GC(Isolate *isolate, U32 heap_size) noexcept : allocator_(isolate),
                                                                    fibers_(nullptr),
                                                                    max_heap_size_(heap_size) {
                if (heap_size < kGCMinHeapSize)
                    this->max_heap_size_ = kGCMinHeapSize;

                this->context_.generations[0].promotion_threshold = kGCPromotionThresholdGen0;
                this->context_.generations[1].promotion_threshold = kGCPromotionThresholdGen1;
            }

            explicit GC(Isolate *isolate) noexcept : GC(isolate, kGCMaxHeapSize) {
            }

            MSize Collect() noexcept {
                return this->Collect(0, kGCGenerations);
            }

            MSize Collect(int start, int end) noexcept;

            void Free(GCHead *head) noexcept;

            static void HeadInsert(GCHead **list, GCHead *head) noexcept;

            static void HeadRemove(const GCHead *head) noexcept;

            void NextEpoch() noexcept;

            void ResetStats(int generation) noexcept;

            void ScanVMRegisters() noexcept;

            void Sweep() noexcept;

            void ScanRoots(const GCGeneration *generation) const noexcept;

            static void Trace(datatype::OObject *object, MSize epoch) noexcept;

            void TraceRoots(GCGeneration *generation, GCTransientList *nextgen, GCTransientList *unreachable) noexcept;

            friend Isolate;

        public:
            /**
             * @brief Triggers a forced garbage collection in the current context.
             *
             * The ForceCollect method immediately initiates a garbage collection cycle, ensuring
             * that unreferenced objects are removed and memory is reclaimed. It operates safely
             * within the context's locking mechanism and guarantees thread-safe execution.
             *
             * @return Returns the total number of objects successfully reclaimed during the garbage collection process.
             */
            MSize ForceCollect() noexcept;

            /**
             * @brief Allocates memory for a new object managed by the garbage collector.
             *
             * This method allocates enough memory to store the requested size along with
             * metadata (such as the GCHead). It also updates the internal account of
             * allocated memory. If allocation fails, it returns a null pointer.
             *
             * Before allocation, the method invokes ThresholdCollect() to check and perform
             * garbage collection if necessary.
             *
             * @param size The size of the object to be allocated, in bytes.
             * @return A pointer to the newly allocated object, or nullptr if allocation fails.
             */
            datatype::OObject *AllocObject(MSize size) noexcept;

            /**
             * @brief Adds a Fiber object to the garbage collector's list for tracking.
             *
             * Registers the provided Fiber instance with the garbage collector, ensuring
             * it is properly managed throughout its lifecycle, including garbage collection
             * processes and resource cleanup.
             *
             * @param fiber A pointer to the Fiber instance to be added for tracking.
             */
            void AddFiber(Fiber *fiber) noexcept;

            /**
             * @brief Frees the memory associated with the given object by invoking the garbage collection mechanism on its header.
             *
             * This function retrieves the garbage collection header of the specified object and delegates the deallocation
             * process to the appropriate garbage collection system.
             *
             * @param object A pointer to the object to be freed. This must be an instance of datatype::OObject.
             */
            void Free(datatype::OObject *object) noexcept {
                this->Free(GC_GET_HEAD(object));
            }

            /**
             * @brief Performs a raw de-allocation of a previously allocated object.
             *
             * This function deallocates the memory associated with an object managed by the garbage collector.
             * The provided object must not be actively tracked and must be manually de-initialized in its internal
             * components unless the `dtor` parameter is set to true. If `dtor` is true, the destructor of the object
             * will be called during this operation to allow proper resource cleanup. Otherwise, this method solely
             * releases the memory allocated for the object.
             *
             * @param object A pointer to the garbage-collected object to be deallocated.
             * @param dtor Indicates whether the destructor of the object should be called (true) or not (false).
             */
            void RawFree(datatype::OObject *object, bool dtor) noexcept;

            /**
             * @brief Removes the specified Fiber instance from the fiber tracking list in the garbage collector.
             *
             * This method ensures the safe removal of the Fiber instance from its internal doubly linked list,
             * updating the necessary pointers to maintain the integrity of the list. It is used during cleanup
             * or when the Fiber is no longer required.
             *
             * @param fiber A pointer to the Fiber instance to be removed from the tracking list.
             */
            void RemoveFiber(const Fiber *fiber) noexcept;

            /**
             * @brief Performs a threshold-based garbage collection process to manage allocated memory.
             *
             * This method monitors memory usage and triggers garbage collection if certain conditions
             * are met. It ensures that the memory usage remains within the defined heap size limits
             * by invoking collection on objects based on their allocation thresholds, generation
             * thresholds, and reference-count conditions. The method also integrates a sweeping phase
             * to finalize the cleanup process.
             *
             * It prevents simultaneous invocation by leveraging a thread-safe mechanism
             * to ensure only one instance of the garbage collection process runs at any given time.
             */
            void ThresholdCollect() noexcept;

            /**
             * @brief Tracks a given object in the garbage collection process.
             *
             * This method ensures that the provided object is registered for tracking
             * in the current generation of objects monitored by the garbage collector.
             *
             * @param object A pointer to the object to be tracked. The object must be properly
             *               initialized and convertible to the expected type.
             * @param is_container Indicates whether the object is a container (true) or a simple object (false).
             *                   Container objects can hold references to other objects and require recursive tracing.
             */
            void Track(datatype::OObject *object, bool is_container) noexcept;
        };
    }
}

#endif // !ORBIT_ORBITER_MEMORY_GC_H_
