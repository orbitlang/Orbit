// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/memory/gc.h>

using namespace orbiter;
using namespace orbiter::memory;

bool GC::Initialize() noexcept {
    this->context_ = this->allocator_.alloc<GCContext>(sizeof(GCContext));
    if (this->context_ == nullptr)
        return false;

    MemoryZero(this->context_->generations, sizeof(GCGeneration) * kGCGenerations);

    this->context_->allocated_bytes = 0;

    return true;
}

void *GC::AllocObject(MSize size) noexcept {
    auto allocate = sizeof(GCHead) + size;

    auto *obj = this->allocator_.alloc<GCHead>(allocate);
    if (obj != nullptr) {
        obj->next = nullptr;
        obj->prev = nullptr;
        obj->r_count = 0;

        this->context_->allocated_bytes += allocate;

        return ((unsigned char *) obj) + sizeof(GCHead);
    }

    return nullptr;
}

void GC::Free(void *ptr) const noexcept {
    const auto *head = (GCHead *) (((unsigned char *) ptr) - sizeof(GCHead));
    const auto size = head->size;

    this->allocator_.free(ptr);

    this->context_->allocated_bytes -= size;
}
