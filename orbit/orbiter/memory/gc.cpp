// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/excstack.h>
#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/memory/gc.h>

using namespace orbiter;
using namespace orbiter::datatype;
using namespace orbiter::memory;

MSize GC::Collect(int start, int end) noexcept {
    GCTransientList unreachable{};
    GCTransientList nextgen[2]{};

    int next_gen = start;
    int chg = 0;

    // Ensure 'start' and 'end' are within valid generation indices
    start = start % kGCGenerations;
    end = end % kGCGenerations;

    assert(start <= end);

    // Move to the next epoch to avoid revisiting old objects
    this->NextEpoch();

    // 1) Scan isolate
    this->ScanIsolate();

    // 2) Scan active fibers and VMs for reachable objects
    this->ScanFibers();

    for (auto i = start; i < end; i++) {
        // Determine the next generation for promoting objects
        if ((next_gen = (i + 1) % kGCGenerations) == 0)
            next_gen = kGCGenerations - 1;

        const auto selected = this->generations_ + i;
        const auto total = selected->count;

        // Reset collection statistics for the current generation
        this->ResetStats(i);

        // The object list is empty, no need to continue
        if (selected->list == nullptr)
            return 0;

        // Increment the number of times this generation has been collected
        selected->times += 1;

        // 3) Scan root elements (objects directly accessible from the generation)
        ScanRoots(selected);

        // 4) Trace and classify unreachable objects
        TraceRoots(selected, &nextgen[1 - chg], &unreachable);

        // Promote objects from the previous generation to the current generation
        if (nextgen[chg].head != nullptr)
            selected->count += nextgen[chg].MergeTo(&selected->list);

        // Alternate between nextgen lists for temporary storage
        chg ^= 1;

        // Update statistics for collection
        selected->collected = total - selected->count;
        selected->uncollected = selected->count;
    }

    // Promote remaining objects in the nextgen list to the next generation
    if (nextgen[chg].head != nullptr) {
        const auto selected = this->generations_ + next_gen;

        selected->count += nextgen[chg].MergeTo(&selected->list);
    }

    // Move unreachable objects to the garbage list
    return unreachable.MergeTo(&this->garbage_);
}

void GC::Free(GCHead *head) noexcept {
    const auto size = head->size;
    auto *obj = GC_GET_OBJ(head);
    auto *dtor = O_GET_TYPE(obj)->dtor;

    if (dtor != nullptr && !head->IsFinalized()) {
        if (!dtor(obj)) {
            head->SetFinalize(true);

            head->prev = nullptr;

            Track(obj, head->IsContainer());

            return;
        }
    }

    O_DECREF(O_GET_TYPE(obj));

    this->allocator_.free(head);

    this->allocated_bytes_ -= size;
}

void GC::HeadInsert(GCHead **list, GCHead *head) noexcept {
    if (*list == nullptr) {
        head->SetNext(nullptr);
        head->prev = list;
    } else {
        head->SetNext(*list);

        if (*list != nullptr)
            (*list)->prev = &head->next;

        head->prev = list;
    }

    *list = head;
}

void GC::HeadRemove(const GCHead *head) noexcept {
    auto *next = head->Next();

    if (head->prev != nullptr)
        *head->prev = next;

    if (next != nullptr)
        next->prev = head->prev;
}

void GC::NextEpoch() noexcept {
    this->epoch_ += 1;

    if (this->epoch_ > kGCMaxEpoch)
        this->epoch_ = 1;
}

void GC::ReleaseSTW() noexcept {
    std::unique_lock lock(this->barrier_lock_);

    this->requested_ = false;

    lock.unlock();

    this->wait_barrier_.notify_all();
}

void GC::RequestSTW() noexcept {
    std::unique_lock lock(this->barrier_lock_);

    if (this->requested_) {
        this->wait_barrier_.wait(lock, [this] {
            return !this->requested_;
        });
    }

    this->requested_ = true;

    this->wait_barrier_.wait(lock, [this] {
        return this->parked_mutators_ == this->mutators_ - 1;
    });
}

void GC::ResetStats(const int generation) noexcept {
    if (generation > 0)
        this->generations_[generation - 1].times = 0;

    auto *gen = this->generations_ + generation;
    gen->collected = 0;
    gen->uncollected = 0;
}

void GC::ScanFibers() const noexcept {
    for (auto *cursor = this->fibers_; cursor != nullptr; cursor = cursor->gc_set.next) {
        // FiberContext
        Visit((OObject *) cursor->context.context, this->epoch_);
        Visit((OObject *) cursor->context.module, this->epoch_);
        Visit((OObject *) cursor->context.code, this->epoch_);
        Visit(cursor->context.func, this->epoch_);

        // PanicContainer
        for (const auto *panic = *cursor->panic.r_current_; panic != nullptr; panic = panic->prev)
            Visit(panic->error, this->epoch_);

        // Defer stack
        for (const auto *defer = cursor->defer_stack.stack_; defer != nullptr; defer = defer->next)
            Visit((OObject *) defer->func, this->epoch_);

        // Future
        Visit(cursor->future, this->epoch_);

        this->ScanVMRegisters(cursor);
        this->ScanVMStack(cursor);
    }
}

void GC::ScanIsolate() const noexcept {
    const auto *isolate = this->allocator_.GetIsolate();

    Visit(isolate->oom_error_, this->epoch_);

    // PanicContainer
    for (const auto *panic = *isolate->panic.r_current_; panic != nullptr; panic = panic->prev)
        Visit(panic->error, this->epoch_);
}

void GC::ScanVMRegisters(Fiber *fiber) const noexcept {
    const auto *regs = (Register *) &fiber->vm.regs;

    for (auto i = 0; i < kGeneralPurposeRegistersCount; i++) {
        auto *obj = (OObject *) (*(PtrSize *) (regs + i));

        if (!O_IS_OBJECT(obj))
            continue;

        Visit(obj, this->epoch_);
    }
}

void GC::ScanVMStack(const Fiber *fiber) const noexcept {
    const auto *stack = fiber->vm.stack.stack;
    const auto SP = fiber->vm.regs.SP.reg;

    for (auto i = 0; i < SP; i += sizeof(PtrSize)) {
        auto *obj = *((OObject **) (stack + i));

        if (*((PtrSize *) (stack + i)) == kExceptionContextTag) {
            obj = (OObject *) ((ExceptionContext *) (stack + i))->ret_value;

            if (O_IS_OBJECT(obj))
                Visit(obj, this->epoch_);

            i += sizeof(ExceptionContext) - sizeof(PtrSize);

            continue;
        }

        if (!O_IS_OBJECT(obj))
            continue;

        Visit(obj, this->epoch_);
    }
}

void GC::ScanRoots(const GCGeneration *generation) const noexcept {
    for (auto *cursor = generation->list; cursor != nullptr; cursor = cursor->Next()) {
        auto *obj = GC_GET_OBJ(cursor);

        if (O_GET_RC(obj).GetCount() > 0) {
            const auto visited = cursor->IsVisited(this->epoch_);

            cursor->epoch = this->epoch_;

            if (!visited && cursor->IsContainer())
                Trace(obj, this->epoch_);
        }
    }
}

void GC::Sweep() noexcept {
    std::unique_lock lock(this->garbage_lock_);

    auto *cursor = this->garbage_;
    this->garbage_ = nullptr;

    lock.unlock();

    while (cursor != nullptr) {
        auto *next = cursor->Next();

        this->Free(cursor);

        cursor = next;
    }
}

void GC::Trace(OObject *object, MSize epoch) noexcept {
    const auto *info = O_GET_TYPE(object);

    assert(info != nullptr);

    do {
        const auto *slots = O_SLOT(object, info);
        const auto slots_count = O_SLOT_COUNT(object, info);

        for (auto i = 0; i < slots_count; i++) {
            auto *obj = slots[i];

            if (!O_IS_OBJECT(obj))
                continue;

            auto *head = GC_GET_HEAD(obj);
            if (!head->IsVisited(epoch)) {
                head->SetVisited(epoch);

                if (head->IsContainer())
                    Trace(obj, epoch);
            }
        }

        if (info->trace != nullptr)
            info->trace(object, Trace, epoch);

        info = info->head_.type_;
    } while (info != nullptr);
}

void GC::TraceRoots(GCGeneration *generation, GCTransientList *nextgen, GCTransientList *unreachable) noexcept {
    const auto last_gen = generation == &this->generations_[kGCGenerations - 1];

    GCHead *next;
    for (auto *cursor = generation->list; cursor != nullptr; cursor = next) {
        next = cursor->Next();

        if (cursor->IsVisited(this->epoch_)) {
            cursor->age += 1;

            if (!last_gen && cursor->age >= generation->promotion_threshold) {
                cursor->age = 0;

                HeadRemove(cursor);
                generation->count -= 1;

                nextgen->AddHead(cursor);
            }

            continue;
        }

        HeadRemove(cursor);
        generation->count -= 1;

        unreachable->AddHead(cursor);

        this->tracked_count_ -= 1;
        this->tracked_bytes_ -= cursor->size;
    }
}

void GC::Visit(OObject *object, const MSize epoch) noexcept {
    if (object == nullptr)
        return;

    auto *head = GC_GET_HEAD(object);
    if (!head->CheckSetVisited(epoch)) {
        if (head->IsContainer())
            Trace(object, epoch);
    }
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

bool GC::ParkAtSafepoint() noexcept {
    std::unique_lock lock(this->barrier_lock_);

    if (!this->requested_)
        return false;

    this->parked_mutators_ += 1;

    this->wait_barrier_.wait(lock, [this] {
        return !this->requested_;
    });

    this->parked_mutators_ -= 1;

    return true;
}

MSize GC::ForceCollect() noexcept {
    this->RequestSTW();

    const auto result = this->Collect();

    this->ReleaseSTW();

    return result;
}

OObject *GC::AllocObject(const MSize size) noexcept {
    const auto allocate = sizeof(GCHead) + size;

    this->ThresholdCollect();

    auto *head = this->allocator_.alloc<GCHead>(allocate);
    if (head != nullptr) {
        new(head)GCHead();

        head->epoch = 0;
        head->age = 0;
        head->size = allocate;

        this->allocated_bytes_ += allocate;

        const auto obj = GC_GET_OBJ(head);

        O_UNSAFE_GET_RC(obj) = (MSize) 0;

        return obj;
    }

    return nullptr;
}

void GC::AddFiber(Fiber *fiber) noexcept {
    std::unique_lock _(this->vm_lock_);

    if (this->fibers_ == nullptr) {
        fiber->gc_set.next = nullptr;
        fiber->gc_set.prev = &this->fibers_;
    } else {
        fiber->gc_set.next = this->fibers_;

        if (this->fibers_ != nullptr)
            this->fibers_->gc_set.prev = &fiber->gc_set.next;

        fiber->gc_set.prev = &this->fibers_;
    }

    this->fibers_ = fiber;
}

void GC::EnterManagedRegion() noexcept {
    std::unique_lock lock(this->barrier_lock_);

    this->wait_barrier_.wait(lock, [this] {
        return !this->requested_;
    });

    this->mutators_ += 1;
}

void GC::LeaveManagedRegion() noexcept {
    std::unique_lock lock(this->barrier_lock_);

    this->mutators_ -= 1;

    lock.unlock();

    // If the GC was waiting, notify the change
    this->wait_barrier_.notify_all();
}

void GC::RawFree(OObject *object, bool dtor) noexcept {
    const auto head = GC_GET_HEAD(object);
    const auto size = head->size;

    if (dtor && O_GET_TYPE(object)->dtor != nullptr)
        O_GET_TYPE(object)->dtor(object);

    O_FAST_DECREF(O_GET_TYPE(object));

    this->allocator_.free(head);

    this->allocated_bytes_ -= size;
}

void GC::RemoveFiber(const Fiber *fiber) noexcept {
    std::unique_lock _(this->vm_lock_);

    auto *next = fiber->gc_set.next;

    if (fiber->gc_set.prev != nullptr)
        *fiber->gc_set.prev = next;

    if (next != nullptr)
        next->gc_set.prev = fiber->gc_set.prev;
}

void GC::ThresholdCollect() noexcept {
    auto allocated_bytes = this->allocated_bytes_.load(std::memory_order_relaxed);

    if (!this->enabled_ || allocated_bytes < ((this->max_heap_size_ * 90) / 100))
        return;

    this->RequestSTW();

    allocated_bytes = this->allocated_bytes_.load(std::memory_order_relaxed);

    if (allocated_bytes < ((this->max_heap_size_ * 90) / 100)) {
        if (allocated_bytes >= (U32) ((this->max_heap_size_ * 40) / 100)) {
            auto i = 1;

            for (; i < kGCGenerations; i++) {
                const auto *generations = this->generations_;

                if (generations[i - 1].times < generations[i].threshold
                    && generations[i].count < kGCThresholdElementsCount)
                    break;
            }

            this->Collect(0, i);
        }
    } else
        this->Collect();

    this->ReleaseSTW();

    this->Sweep();
}

void GC::Track(OObject *object, bool is_container) noexcept {
    auto *head = GC_GET_HEAD(object);

    auto *generation = this->generations_;

    std::unique_lock _(this->track_lock_);

    if (!head->IsTracked()) {
        if (is_container)
            head->SetContainerType();

        HeadInsert(&generation->list, head);
        generation->count += 1;

        this->tracked_count_ += 1;
        this->tracked_bytes_ += head->size;
    }
}
