// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>

#include <orbit/orbiter/fiber.h>

#include <orbit/orbiter/memory/gc.h>

using namespace orbiter;
using namespace orbiter::datatype;
using namespace orbiter::memory;

thread_local bool rc_trashing = false;

void InitGCRefCount(GCHead *head) {
    head->r_count = O_GET_RC(GC_GET_OBJ(head)).GetStrongCount();
    head->SetVisited(true);
}

void GCDecRef(OObject *self) {
    if (self == nullptr || !O_IS_OBJECT(self) || !RC_CHECK_IS_GCOBJ(O_UNSAFE_GET_RC(self)))
        return;

    auto *head = GC_GET_HEAD(self);

    if (!head->IsVisited())
        InitGCRefCount(head);

    head->r_count -= 1;
}

void GCIncRef(OObject *self) {
    if (self == nullptr || !O_IS_OBJECT(self) || !RC_CHECK_IS_GCOBJ(O_UNSAFE_GET_RC(self)))
        return;

    auto *head = GC_GET_HEAD(self);

    if (head->IsVisited()) {
        head->SetVisited(false);

        O_GET_TYPE(self)->trace(self, GCIncRef);
    }

    head->r_count += 1;
}

// *********************************************************************************************************************
// PRIVATE
// *********************************************************************************************************************

MSize GC::CollectRCQueue() noexcept {
    MSize count = 0;
    MSize bytes = 0;

    this->ScanVMRegisters();

    rc_trashing = true;

    GCHead *next;
    for (auto *cursor = this->context_.rel_list; cursor != nullptr; cursor = next) {
        next = cursor->Next();

        // If the reference count is zero, the object is unused and not in VM registers
        if (cursor->r_count == 0) {
            HeadRemove(cursor);

            bytes += cursor->size;

            // TODO: Call dtor and release memory

            this->Free(cursor);

            count += 1;

            continue;
        }

        // Check the strong reference count of the object to determine its active usage
        MSize r_count = O_GET_RC(GC_GET_OBJ(cursor)).GetStrongCount();
        if (r_count > 0) {
            // If the strong reference count is greater than zero, the object is actively used.
            // This means it is not only potentially in a VM register but might also
            // be part of a container like a list, dictionary, or other complex structure.
            // In this case, remove it from the release list to ensure it is not mistakenly collected.
            HeadRemove(cursor);

            bytes += cursor->size;
        }

        // Reset the r_count for the next cycle to prepare it for a future check
        cursor->r_count = 0;
    }

    rc_trashing = false;

    this->context_.rel_count -= count;
    this->context_.rel_bytes -= bytes;

    return count;
}

void GC::Free(GCHead *head) noexcept {
    const auto size = head->size;

    this->allocator_.free(head);

    this->context_.allocated_bytes -= size;
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

void GC::ResetStats(int generation) noexcept {
    if (generation > 0)
        this->context_.generations[generation - 1].times = 0;

    auto *gen = this->context_.generations + generation;
    gen->collected = 0;
    gen->uncollected = 0;
}

void GC::ScanVMRegisters() noexcept {
    std::unique_lock _(this->context_.vm_lock);

    for (const auto *cursor = this->fibers_; cursor != nullptr; cursor = cursor->gc_set.next) {
        const auto *regs = (Register *) &cursor->vm.regs;

        for (auto i = 0; i < sizeof(Registers); i++) {
            auto *obj = (OObject *) ((PtrSize *) (regs + i));

            if (obj == nullptr || !O_IS_OBJECT(obj))
                continue;

            auto *head = GC_GET_HEAD(obj);
            head->r_count += 1;
        }
    }
}

void GC::Trace(OObject *object, bool inc) noexcept {
    const auto *info = O_GET_TYPE(object);

    assert(info != nullptr);

    do {
        const auto *slots = O_SLOT(object, info);
        const auto slots_count = O_SLOT_COUNT(object, info);

        for (auto i = 0; i < slots_count; i++) {
            auto *obj = slots[i];

            if (O_IS_OBJECT(obj) && RC_CHECK_IS_GCOBJ(O_UNSAFE_GET_RC(obj))) {
                auto *head = GC_GET_HEAD(obj);

                if (!inc) {
                    if (!head->IsVisited())
                        InitGCRefCount(head);

                    head->r_count -= 1;

                    continue;
                }

                if (head->IsVisited()) {
                    head->SetVisited(false);

                    Trace(obj, true);
                }

                head->r_count += 1;
            }
        }

        if (info->trace != nullptr)
            info->trace(object, inc ? GCIncRef : GCDecRef);

        info = info->head_.type_;
    } while (info != nullptr);
}

void GC::TraceRoots(GCGeneration *generation, GCHead **unreachable) noexcept {
    GCHead *next;

    for (auto *cursor = generation->list; cursor != nullptr; cursor = next) {
        next = cursor->Next();

        if (cursor->r_count == 0) {
            cursor->SetFinalize(true);

            HeadRemove(cursor);
            HeadInsert(unreachable, cursor);

            generation->count -= 1;

            continue;
        }

        if (cursor->IsVisited()) {
            cursor->SetVisited(false);

            Trace(GC_GET_OBJ(cursor), true);
        }
    }
}

void GC::Trashing(GCGeneration *nextgen, GCHead *unreachable) noexcept {
    GCHead *next;

    for (auto *cursor = unreachable; cursor != nullptr; cursor = next) {
        next = cursor->Next();

        HeadRemove(cursor);

        // Check if objects are really unreachable
        if (cursor->r_count == 0) {
            // TODO: garbage lock
            //HeadInsert()

            this->context_.tracked_count -= 1;
            this->context_.tracked_bytes -= cursor->size;

            continue;
        }

        cursor->SetFinalize(false);

        HeadInsert(&nextgen->list, cursor);
        nextgen->count += 1;
    }
}

void GC::SearchRoots(const GCGeneration *generation) noexcept {
    for (auto *cursor = generation->list; cursor != nullptr; cursor = cursor->Next()) {
        auto *obj = GC_GET_OBJ(cursor);

        if (!cursor->IsVisited())
            InitGCRefCount(cursor);

        Trace(obj, false);
    }
}

// *********************************************************************************************************************
// PUBLIC
// *********************************************************************************************************************

MSize GC::Collect() noexcept {
    MSize collected = 0;

    for (auto i = 0; i < kGCGenerations; i++)
        collected += this->Collect(i);

    return collected;
}

MSize GC::Collect(int generation) noexcept {
    GCHead *unreachable = nullptr;

    generation = generation % kGCGenerations;

    int next_gen;
    if ((next_gen = (generation + 1) % kGCGenerations) == 0)
        next_gen = kGCGenerations - 1;

    const auto selected = this->context_.generations + generation;

    this->ResetStats(generation);

    if (selected->list == nullptr)
        return 0;

    selected->times += 1;

    const auto total = selected->count;

    // 1) Enumerate roots
    SearchRoots(selected);

    // 2) Trace all objects reachable from roots
    TraceRoots(selected, &unreachable);

    // 3) Scan all VMs Registers
    this->ScanVMRegisters();

    // 4) Collect unreachable objects
    Trashing(this->context_.generations + next_gen, unreachable);

    selected->collected = total - selected->count;
    selected->uncollected = selected->count;

    return selected->collected;
}

OObject *GC::AllocObject(MSize size) noexcept {
    auto allocate = sizeof(GCHead) + size;

    this->ThresholdCollect();

    auto *obj = this->allocator_.alloc<GCHead>(allocate);
    if (obj != nullptr) {
        new(obj)GCHead();

        obj->size = allocate;

        this->context_.allocated_bytes += allocate;

        return GC_GET_OBJ(obj);
    }

    return nullptr;
}

void GC::AddFiber(Fiber *fiber) noexcept {
    std::unique_lock _(this->context_.vm_lock);

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

void GC::MarkForCollection(OObject *object) noexcept {
    auto *head = GC_GET_HEAD(object);

    if (rc_trashing) {
        HeadInsert(&this->context_.rel_list, head);

        this->context_.rel_count++;
        this->context_.rel_bytes += head->size;

        return;
    }

    std::unique_lock _(this->context_.rel_lock);

    HeadInsert(&this->context_.rel_list, head);

    this->context_.rel_count++;
    this->context_.rel_bytes += head->size;
}

void GC::RemoveFiber(Fiber *fiber) noexcept {
    std::unique_lock _(this->context_.vm_lock);

    auto *next = fiber->gc_set.next;

    if (fiber->gc_set.prev != nullptr)
        *fiber->gc_set.prev = next;

    if (next != nullptr)
        next->gc_set.prev = fiber->gc_set.prev;
}

void GC::ThresholdCollect() noexcept {
    auto allocated_bytes = this->context_.allocated_bytes.load(std::memory_order_relaxed);

    if (this->enabled_) {
        if (allocated_bytes < ((this->max_heap_size_ * 90) / 100)) {
            if (allocated_bytes >= (U32) ((this->max_heap_size_ * 40) / 100)) {
                this->Collect(0);

                for (unsigned short i = 1; i < kGCGenerations; i++) {
                    const auto *generations = this->context_.generations;

                    if (generations[i - 1].times < generations[i].threshold
                        && generations[i].count < kGCThresholdElementsCount)
                        break;

                    Collect(i);
                }
            }
        } else
            this->Collect();
    }

    auto rc_max_memory = this->max_heap_size_ - this->context_.tracked_bytes;

    if (this->context_.rel_bytes < ((rc_max_memory * 20) / 100)
        && this->context_.rel_count < kGCRCThresholdElementsCount)
        return;

    this->CollectRCQueue();
}

void GC::Track(OObject *object) noexcept {
    auto *head = GC_GET_HEAD(object);

    std::unique_lock _(this->context_.track_lock);

    if (!head->IsTracked()) {
        auto *generation = this->context_.generations;

        HeadInsert(&generation->list, head);
        generation->count++;

        this->context_.tracked_count++;
        this->context_.tracked_bytes += head->size;
    }
}
