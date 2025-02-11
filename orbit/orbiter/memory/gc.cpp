// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/memory/gc.h>

using namespace orbiter;
using namespace orbiter::memory;

MSize GC::CollectRCQueue() noexcept {
    MSize count = 0;
    MSize bytes = 0;

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

datatype::OObject *GC::AllocObject(MSize size) noexcept {
    auto allocate = sizeof(GCHead) + size;

    auto *obj = this->allocator_.alloc<GCHead>(allocate);
    if (obj != nullptr) {
        new(obj)GCHead();

        obj->size = allocate;

        this->context_.allocated_bytes += allocate;

        return GC_GET_OBJ(obj);
    }

    return nullptr;
}

void GC::MarkForCollection(datatype::OObject *object) noexcept {
    auto *head = GC_GET_HEAD(object);

    // TODO: recursive mutex!

    std::unique_lock _(this->context_.rel_lock);

    HeadInsert(&this->context_.rel_list, head);

    this->context_.rel_count++;
    this->context_.rel_bytes += head->size;
}

void GC::ThresholdCollect() noexcept {
    // TODO STUB
}

void GC::Track(datatype::OObject *object) noexcept {
    auto *head = GC_GET_HEAD(object);

    this->ThresholdCollect();

    std::unique_lock _(this->context_.track_lock);
    if (!head->IsTracked()) {
        auto *generation = this->context_.generations;

        HeadInsert(&generation->list, head);
        generation->count++;

        this->context_.total_tracked++;
    }
}
