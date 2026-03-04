// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_RUNTIME_H_
#define ORBIT_ORBITER_RUNTIME_H_

#include <thread>

#include <orbit/orbiter/config.h>

#include <orbit/orbiter/fqueue.h>

namespace orbiter {
    constexpr unsigned int kVCoreDefault = 4;
    constexpr unsigned int kVCoreQueueLengthMax = 255;
    constexpr unsigned int kGlobalQueueLengthMax = 300;

    class VCore {
    public:
        VCore *next = nullptr;
        VCore **prev = nullptr;

        bool wired = false;

        FiberQueue<> queue;

        VCore() : queue(kVCoreQueueLengthMax) {
        }
    };

    class OSThread {
        std::thread thread;

        OSThread *next = nullptr;
        OSThread **prev = nullptr;

        Fiber *fiber = nullptr;
        VCore *current = nullptr;
        VCore *old = nullptr;

        bool idle = true;
        bool spinning = false;

        friend class Orbiter;

    public:
        void PushToQueue(OSThread **list) noexcept {
            if (*list == nullptr) {
                this->next = nullptr;
                this->prev = list;
            } else {
                this->next = (*list);

                if ((*list) != nullptr)
                    (*list)->prev = &this->next;

                this->prev = list;
            }

            *list = this;
        }

        void RemoveFromQueue() noexcept {
            if (this->prev != nullptr)
                *this->prev = this->next;
            if (this->next != nullptr)
                this->next->prev = this->prev;

            this->next = nullptr;
            this->prev = nullptr;
        }
    };

    class Orbiter {
        std::mutex ost_lock_;
        std::mutex vcore_lock_;

        std::condition_variable ost_cond_;

        FiberQueue<> fiber_queue_;

        static inline Orbiter *orbiter_ = nullptr;

        // OSThread variables
        OSThread *ost_active_ = nullptr; // Working OSThread
        OSThread *ost_idle_ = nullptr; // IDLE OSThread

        // VCores variables
        VCore *vcores_ = nullptr; // List of instantiated VCore
        VCore *vcores_idle_ = nullptr; // Active VCore

        // Runtime Counters
        unsigned int ost_total_ = 0; // OSThread counter
        unsigned int ost_idle_count_ = 0; // OSThread counter (idle)
        unsigned int ost_max_ = 0; // Maximum OS thread allowed

        unsigned int vcores_count_ = 0;
        unsigned int vcore_unwired_count_ = 0;

        Orbiter() : fiber_queue_(kGlobalQueueLengthMax) {
        }

        /**
         * @brief Attempts to acquire a VCore for the given operating system thread (OSThread).
         *
         * This function first checks for available VCores in the idle list, which are those
         * currently associated with fibers. If an idle VCore is found and successfully wired to
         * the given OSThread, the function returns true. If no suitable VCore is found in the
         * idle list, the function proceeds to search in the complete list of VCores. If a match
         * is found during this search, it also attempts to wire the VCore to the OSThread. If
         * successful, the function returns true.
         *
         * If no VCores are successfully acquired after checking both the idle and complete lists,
         * the function returns false.
         *
         * @param ost A pointer to the OSThread instance for which the VCore is being acquired.
         * @return true if a VCore was successfully acquired and wired to the OSThread, otherwise false.
         */
        bool AcquireVCore(OSThread *ost) noexcept;

        bool InitVCores(unsigned int n) noexcept;

        /**
         * @brief Wires the specified VCore to the given OSThread.
         *
         * This function attempts to associate the provided VCore with the specified
         * OSThread, marking the VCore as wired and updating the thread's current
         * VCore reference. If the VCore is already wired or null, the function returns
         * false. Otherwise, it performs necessary adjustments to the VCore's state
         * in the idle list and decrements the count of idle VCores.
         *
         * @param ost A pointer to the OSThread instance to which the VCore will be wired.
         * @param vcore A pointer to the VCore instance to be wired.
         * @return true if the wiring process was successful, otherwise false.
         */
        bool WireVCore(OSThread *ost, VCore *vcore) noexcept;

        OSThread *AllocOSThread() noexcept;

        void Scheduler(OSThread *ost) noexcept;

        /**
         * @brief Releases and deallocates the specified operating system thread (OSThread).
         *
         * This function ensures that the provided OSThread is valid, is not associated with
         * the current thread, and cannot be joined. Once these conditions are verified, the
         * OSThread is deleted, and the total count of OSThreads managed by the Orbiter instance
         * is decremented.
         *
         * @param ost A pointer to the OSThread instance to be released. If nullptr, the function takes no action.
         */
        void FreeOSThread(const OSThread *ost) noexcept;

        /**
         * @brief Moves the given operating system thread (OSThread) from the active state to the idle state.
         *
         * This method transitions an OSThread instance to the idle state by releasing its associated VCore
         * (if one is currently assigned), marking it as idle, and updating its position in the OSThread queue.
         * The idle thread is added to the idle queue and the idle thread count is incremented accordingly.
         *
         * @param ost A pointer to the OSThread instance that is to be transitioned to the idle state.
         */
        void OSTActive2Idle(OSThread *ost) noexcept;

        /**
         * @brief Moves an operating system thread (OSThread) from the idle state to the active state.
         *
         * This method transitions the specified OSThread from the idle state to the active state
         * by first verifying its current state. If the OSThread is flagged as idle, it removes
         * the thread from the idle queue, updates its state to active, and places it into the active queue.
         * Additionally, the total count of idle threads is decremented.
         *
         * @param ost A pointer to the OSThread instance that needs to transition from idle to active state.
         */
        void OSTIdle2Active(OSThread *ost) noexcept;

        /**
         * @brief Wakes up or initializes an operating system thread (OSThread) to handle queued fibers.
         *
         * This method handles the activation or creation of OSThreads to process fibers
         * from the fiber queue when resources permit. If there is already an idle OSThread
         * available, it is woken up. If not, and the maximum set limit of OSThreads has not
         * been reached, a new OSThread is allocated, initialized, and possibly assigned to
         * a VCore.
         *
         * The method ensures thread safety by utilizing multiple locks and checks internal
         * counters for active and idle OSThreads. If a new OSThread is successfully created
         * and assigned a VCore, it is added to the active queue. Otherwise, it is pushed
         * to the idle queue and remains available for future operations.
         *
         *
         * @note The function internally manages synchronization through mutex locks to
         *       ensure thread-safe operations on shared resources.
         */
        void OSTWakeRun() noexcept;

        /**
         * @brief Releases the VCore wired to a given operating system thread (OSThread).
         *
         * This function disconnects the currently wired VCore from the specified OSThread
         * and adjusts its state. If the OSThread does not have a VCore currently wired,
         * the function simply returns. Once released, the VCore is added back to the idle
         * list if there are pending fibers in its queue. The function also increments the
         * count of unwired VCores to maintain synchronization of resource tracking.
         *
         * @param ost A pointer to the OSThread instance whose VCore is being released.
         */
        void ReleaseVCore(OSThread *ost) noexcept;

    public:
        Orbiter(const Orbiter &) = delete;

        Orbiter &operator=(const Orbiter &) = delete;

        Orbiter(Orbiter &&) = delete;

        Orbiter &operator=(Orbiter &&) = delete;

        ~Orbiter();

        bool Finalize() noexcept;

        static bool Initialize(const void *config) noexcept;

        datatype::HOObject Eval(datatype::Context *context, datatype::Module *module, datatype::Code *code) noexcept;

        static Orbiter *GetInstance() noexcept {
            return orbiter_;
        }
    };
}

#endif // !ORBIT_ORBITER_RUNTIME_H_
