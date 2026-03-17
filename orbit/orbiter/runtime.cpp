// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <random>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/future.h>

#include <orbit/orbiter/fpool.h>

#include <orbit/orbiter/runtime.h>

using namespace orbiter;
using namespace orbiter::datatype;

thread_local OSThread *orbiter::ost_self = nullptr;

bool Orbiter::AcquireVCore(OSThread *ost) noexcept {
    // First, look at the "idle" VCores, which are those that have available fibers.
    for (auto *cursor = this->vcores_idle_; cursor != nullptr; cursor = cursor->next) {
        if (WireVCore(ost, cursor))
            return true;
    }

    // Search through all VCores
    for (auto i = 0; i < this->vcores_count_; i++) {
        if (WireVCore(ost, this->vcores_ + i))
            return true;
    }

    return false;
}

bool Orbiter::InitVCores(unsigned int n) noexcept {
    if (n == 0) {
        n = std::thread::hardware_concurrency();
        if (n == 0)
            n = kVCoreDefault;
    }

    this->vcores_ = new(std::nothrow) VCore[n]();
    this->vcores_count_ = n;
    this->vcore_unwired_count_ = n;

    return this->vcores_ != nullptr;
}

bool Orbiter::WireVCore(OSThread *ost, VCore *vcore) noexcept {
    if (vcore == nullptr || vcore->wired)
        return false;

    vcore->wired = true;

    if (vcore->prev != nullptr) {
        *(vcore->prev) = vcore->next;

        if (vcore->next != nullptr)
            vcore->next->prev = vcore->prev;

        vcore->next = nullptr;
        vcore->prev = nullptr;
    }

    ost->current = vcore;
    ost->old = nullptr;

    this->vcore_unwired_count_ -= 1;

    return true;
}

Fiber *Orbiter::FindExecutable(OSThread *ost, Fiber *last, const bool global_first) noexcept {
    Fiber *fiber = nullptr;

    if (should_exit_)
        return nullptr;

    if (!global_first) {
        fiber = ost->current->queue.Dequeue();
        if (fiber != nullptr)
            return fiber;
    }

    // Check from global queue
    if ((fiber = this->fiber_queue_.Dequeue()) != nullptr)
        return fiber;

    if (global_first) {
        fiber = ost->current->queue.Dequeue();
        if (fiber != nullptr)
            return fiber;
    }

    if (last != nullptr)
        return last;

    std::unique_lock lock(this->ost_lock_);

    const auto vcore_busy = this->vcores_count_ / 2;
    if (this->ost_spinning_count_ + 1 > vcore_busy)
        return nullptr;

    ost->spinning = true;
    this->ost_spinning_count_ += 1;

    ost->current->stealable = false;

    lock.unlock();

    for (auto i = 0; i < kSpinningCheckMax; i++) {
        if ((fiber = this->StealWork(ost)) != nullptr)
            break;

        // Check from global queue
        if ((fiber = this->fiber_queue_.Dequeue()) != nullptr)
            break;
    }

    lock.lock();

    ost->spinning = false;
    this->ost_spinning_count_ -= 1;

    ost->current->stealable = true;

    // Last check on global queue
    if (fiber == nullptr)
        fiber = this->fiber_queue_.Dequeue();

    return fiber;
}

Fiber *Orbiter::StealWork(const OSThread *ost) const noexcept {
    thread_local std::minstd_rand vc_random([] {
        std::random_device rd;
        return rd();
    }());

    std::uniform_int_distribution<unsigned int> r_distrib(0, this->vcores_count_ - 1);

    auto *vc_current = ost->current;

    const auto start = r_distrib(vc_random);
    for (auto i = 0; i < this->vcores_count_; i++) {
        auto *target_vc = this->vcores_ + ((start + i) % this->vcores_count_);
        if (target_vc == vc_current || !target_vc->stealable)
            continue;

        // Steal from queues that contain one or more items
        auto *fiber = vc_current->queue.StealDequeue(1, target_vc->queue);
        if (fiber != nullptr)
            return fiber;
    }

    return nullptr;
}

OSThread *Orbiter::AllocOSThread() noexcept {
    auto *ost = new(std::nothrow) OSThread();
    if (ost != nullptr)
        this->ost_total_ += 1;

    return ost;
}

void Orbiter::AcquireVCoreOrSuspend(OSThread *ost) noexcept {
    std::unique_lock lock(this->vcore_lock_);

    while (ost->current == nullptr && !this->should_exit_) {
        if (WireVCore(ost, ost->old) || AcquireVCore(ost)) {
            lock.unlock();

            this->OSTIdle2Active(ost);

            break;
        }

        lock.unlock();
        this->OSTActive2Idle(ost);

        this->OSTSleep();
        lock.lock();
    }
}

void Orbiter::Scheduler(OSThread *ost) noexcept {
    Fiber *last = nullptr;
    const Isolate *last_isolate = nullptr;

    ost_self = ost;

    unsigned int fairness_tick = kFairnessTickCount;

    while (!this->should_exit_) {
        last = ost->fiber;

        if (fairness_tick == 0)
            fairness_tick = kFairnessTickCount;

        if (ost->current == nullptr)
            this->AcquireVCoreOrSuspend(ost);

        ost->fiber = this->FindExecutable(ost, last, --fairness_tick == 0);
        if (ost->fiber == nullptr) {
            if (should_exit_)
                break;

            if (last_isolate != nullptr) {
                last_isolate->gc->LeaveManagedRegion();
                last_isolate = nullptr;
            }

            this->OSTActive2Idle(ost);
            this->OSTSleep();

            continue;
        }

        if (last != nullptr && last != ost->fiber) {
            if (!ost->current->queue.Enqueue(last))
                this->fiber_queue_.Enqueue(last);
        }

        // Check the fiber is not yet connected to a previous OSThread,
        // this can happen when the fiber is interrupted by an asynchronous operation (e.g. socket read/write).
        // Such an operation may complete before the thread that started it has actually released the fiber.
        if (ost->fiber->active_ost != nullptr) {
            std::this_thread::yield();

            continue;
        }

        const auto fiber = ost->fiber;

        if (last_isolate != fiber->isolate) {
            if (last_isolate != nullptr)
                last_isolate->gc->LeaveManagedRegion();

            fiber->isolate->gc->EnterManagedRegion();

            last_isolate = fiber->isolate;
        }

        fiber->active_ost = ost;

        Fiber::SetCurrent(fiber);

        auto *result = eval(fiber);

        fiber->isolate->gc->ParkAtSafepoint();

        fiber->active_ost = nullptr;

        if (fiber->state == FiberState::RUNNING) {
            PublishResult(ost, result);

            ost->fiber = nullptr;

            continue;
        }

        if (fiber->state == FiberState::SUSPENDED) {
            ost->fiber = nullptr;

            continue;
        }

        assert(fiber->state == FiberState::YIELDED);

        fiber->vm.preempt_tick = kPreemptTick; // TODO: get from config
    }

    if (last_isolate != nullptr)
        last_isolate->gc->LeaveManagedRegion();

    std::unique_lock vc_lock(this->vcore_lock_);

    this->ReleaseVCore(ost);

    vc_lock.unlock();

    std::unique_lock _(this->ost_lock_);

    ost->RemoveFromQueue();

    ost->thread.detach();

    this->FreeOSThread(ost);
}

void Orbiter::FreeOSThread(const OSThread *ost) noexcept {
    if (ost != nullptr) {
        assert(ost->thread.get_id() != std::this_thread::get_id());
        assert(!ost->thread.joinable());

        delete ost;

        this->ost_total_ -= 1;
    }
}

void Orbiter::OSTActive2Idle(OSThread *ost) noexcept {
    if (ost->idle)
        return;

    std::unique_lock vc_lock(this->vcore_lock_);
    std::unique_lock _(this->ost_lock_);

    this->ReleaseVCore(ost);
    vc_lock.unlock();

    ost->idle = true;

    ost->RemoveFromQueue();
    ost->PushToQueue(&this->ost_idle_);

    this->ost_idle_count_ += 1;
}

void Orbiter::OSTIdle2Active(OSThread *ost) noexcept {
    if (!ost->idle)
        return;

    std::unique_lock _(this->ost_lock_);

    ost->idle = false;

    ost->RemoveFromQueue();
    ost->PushToQueue(&this->ost_active_);

    this->ost_idle_count_ -= 1;
}

void Orbiter::OSTSleep() noexcept {
    std::unique_lock lock(this->ost_lock_);

    this->ost_cond_.wait(lock, [&] {
        if (this->ost_pending_wakeups_ > 0) {
            this->ost_pending_wakeups_ -= 1;

            return true;
        }

        return this->should_exit_;
    });
}

void Orbiter::OSTWakeRun() noexcept {
    std::unique_lock vc_lock(this->vcore_lock_);

    if (this->fiber_queue_.IsEmpty() && this->vcores_idle_ == nullptr)
        return;

    std::unique_lock _(this->ost_lock_);

    if (this->ost_idle_ != nullptr) {
        if (this->ost_spinning_count_ == 0) {
            this->ost_pending_wakeups_ += 1;

            this->ost_cond_.notify_one();
        }

        return;
    }

    if (this->ost_total_ + 1 > this->ost_max_)
        return;

    auto *ost = this->AllocOSThread();
    if (ost == nullptr) {
        vc_lock.unlock();

        // TODO: error
        assert(false);
        return;
    }

    const auto acquired = this->AcquireVCore(ost);

    vc_lock.unlock();

    if (acquired) {
        ost->idle = false;
        ost->PushToQueue(&this->ost_active_);
    } else {
        ost->PushToQueue(&this->ost_idle_);
        this->ost_idle_count_ += 1;
    }

    ost->thread = std::thread(&Orbiter::Scheduler, this, ost);
}

void Orbiter::PublishResult(const OSThread *ost, OObject *result) noexcept {
    auto *fiber = ost->fiber;

    if (fiber->future != nullptr) {
        if (*fiber->panic.r_current_ != nullptr)
            FutureReject((Future *) fiber->future, fiber->GetDiscardPanic().get());
        else
            FutureResolve((Future *) fiber->future, result);
    }

    fiber->isolate->gc->RemoveFiber(fiber);

    fiber->isolate->fpool_->DeleteFiber(fiber);
}

void Orbiter::ReleaseVCore(OSThread *ost) noexcept {
    const auto vcore = ost->current;

    if (vcore == nullptr)
        return;

    ost->old = ost->current;
    ost->current = nullptr;

    if (!vcore->queue.IsEmpty()) {
        auto *next = &this->vcores_idle_;

        while (*next != nullptr)
            next = &(*next)->next;

        (*next) = vcore;
        vcore->prev = next;
    }

    vcore->wired = false;

    this->vcore_unwired_count_ += 1;
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

Orbiter::~Orbiter() {
    // TODO: fill this!

    delete[] this->vcores_;
}

bool Orbiter::Finalize() noexcept {
    // TODO: fill this!
    return false;
}

bool Orbiter::Initialize(const void *config) noexcept {
    if (orbiter_ != nullptr)
        return true;

    orbiter_ = new(std::nothrow) Orbiter();
    if (orbiter_ != nullptr) {
        if (!orbiter_->InitVCores(kVCoreDefault))
            return false; // TODO: from config!

        orbiter_->ost_max_ = 4; // FIXME
        return true;
    }

    return false;
}

HOObject Orbiter::Eval(Context *context, Module *module, Code *code) noexcept {
    auto *isolate = O_GET_ISOLATE(code);

    // Sanity check
    assert(O_GET_ISOLATE(context) == isolate);
    if (module != nullptr)
        assert(O_GET_ISOLATE(module) == isolate);

    auto *fiber = isolate->fpool_->NewFiber();
    if (fiber == nullptr)
        return HOObject(nullptr);

    const auto future = FutureNew(isolate);
    if (!future) {
        isolate->fpool_->DeleteFiber(fiber);

        return HOObject(nullptr);
    }

    fiber->SetContext(context, module, code);
    fiber->future = (OObject *) future.get();

    isolate->gc->AddFiber(fiber);

    if (!this->fiber_queue_.Enqueue(fiber)) {
        isolate->gc->RemoveFiber(fiber);
        isolate->fpool_->DeleteFiber(fiber);

        ErrorSet(isolate,
                 SchedulerError::Details[SchedulerError::Reason::ID],
                 nullptr,
                 SchedulerError::Details[SchedulerError::Reason::FIBER_QUEUE_FULL]);

        return HOObject(nullptr);
    }

    this->OSTWakeRun();

    // Note: One might consider releasing fibers from this queue at this point.
    // However, this is unnecessary due to the work-stealing mechanism already in place.

    FutureAwait(future.get());

    return HOObject{future->result};
}

HFuture Orbiter::EvalAsync(Function *func, const unsigned char *stack_begin, const U16 size) noexcept {
    auto *isolate = O_GET_ISOLATE(func);

    // Allocate a new fiber to run the async function. Unlike Eval(), this does not block
    // the caller — the function executes concurrently on a scheduler thread.
    auto *fiber = isolate->fpool_->NewFiber();
    if (fiber == nullptr)
        return HFuture(nullptr);

    // Create the future that will carry the result once the fiber completes.
    // The caller holds this handle and can await it at any point.
    const auto future = FutureNew(isolate);
    if (!future) {
        isolate->fpool_->DeleteFiber(fiber);

        return HFuture(nullptr);
    }

    fiber->SetContext(func);
    fiber->future = (OObject *) future.get();

    // Transfer the caller's stack arguments into the new fiber's stack.
    // Arguments have already been evaluated and pushed by the caller, so we copy
    // them verbatim. The fiber begins execution as if it entered the function through
    // a normal call frame, with BP and SP positioned past the arguments and prologue.
    auto *BP = &fiber->vm.regs.BP.reg;
    auto *SP = &fiber->vm.regs.SP.reg;

    auto *stack_dst = fiber->vm.stack.stack + *SP;

    // TODO: No INCREF here — the GC will handle object lifetime in a future revision.
    memory::MemoryCopy(stack_dst, stack_begin, size);

    // Zero the call frame prologue (saved BP, return address slot) so the fiber
    // unwinds cleanly when the function returns.
    memory::MemoryZero(stack_dst + size, kStackPrologueOffset);

    (*BP) += size + kStackPrologueOffset;
    (*SP) += size + kStackPrologueOffset;

    // Register the fiber with the GC before scheduling so it is tracked for
    // the entire duration of its execution.
    isolate->gc->AddFiber(fiber);

    // Submit the fiber to the global run queue. If the queue is full, clean up and
    // surface an error to the caller rather than silently dropping the task.
    if (!this->fiber_queue_.Enqueue(fiber)) {
        isolate->gc->RemoveFiber(fiber);
        isolate->fpool_->DeleteFiber(fiber);

        ErrorSet(isolate,
                 SchedulerError::Details[SchedulerError::Reason::ID],
                 nullptr,
                 SchedulerError::Details[SchedulerError::Reason::FIBER_QUEUE_FULL]);

        return HFuture(nullptr);
    }

    // Signal the scheduler that new work is available. This either wakes an idle
    // OSThread or spins up a new one if the thread pool has not yet reached its limit.
    this->OSTWakeRun();

    return future;
}

void Orbiter::RuntimeDiscardPanic(Isolate *isolate) {
    auto *fiber = Fiber::Current();
    if (fiber != nullptr) {
        fiber->DiscardPanic();

        return;
    }

    isolate->panic.DiscardPanic(&isolate->panic_cache);
}

void Orbiter::RuntimeOOMPanic(Isolate *isolate) noexcept {
    auto *fiber = Fiber::Current();
    if (fiber != nullptr) {
        auto *p = fiber->panic.CreateOOMPanic(isolate, &fiber->panic_cache);
        p->frame = fiber->vm.regs.BP.reg;

        return;
    }

    isolate->panic.CreateOOMPanic(isolate, &isolate->panic_cache);
}

void Orbiter::RuntimePanic(Isolate *isolate, OObject *error) {
    auto *fiber = Fiber::Current();
    if (fiber != nullptr) {
        fiber->Panic(error);

        return;
    }

    isolate->panic.CreatePanic(isolate, &isolate->panic_cache, error);
}
