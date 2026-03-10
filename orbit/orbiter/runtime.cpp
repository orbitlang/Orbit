// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/future.h>

#include <orbit/orbiter/fpool.h>

#include <orbit/orbiter/runtime.h>

using namespace orbiter;
using namespace orbiter::datatype;

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

OSThread *Orbiter::AllocOSThread() noexcept {
    auto *ost = new(std::nothrow) OSThread();
    if (ost != nullptr)
        this->ost_total_ += 1;

    return ost;
}

void Orbiter::Scheduler(OSThread *ost) noexcept {
    // TODO: impl me
    assert(false);
    // Fiber::SetCurrent(...);
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
    std::unique_lock _(ost_lock_);

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

    std::unique_lock _(ost_lock_);

    ost->idle = false;

    ost->RemoveFromQueue();
    ost->PushToQueue(&this->ost_active_);

    this->ost_idle_count_ -= 1;
}

void Orbiter::OSTWakeRun() noexcept {
    std::unique_lock vc_lock(this->vcore_lock_);

    if (this->fiber_queue_.IsEmpty() && this->vcores_idle_ == nullptr)
        return;

    std::unique_lock _(this->ost_lock_);

    if (this->ost_idle_ != nullptr) {
        this->ost_cond_.notify_one();
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
