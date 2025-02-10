// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/isolate.h>

#include <orbit/orbiter/memory/iallocator.h>

using namespace orbiter::memory;

IsolateAllocator::IsolateAllocator(Isolate *isolate) noexcept : isolate_(isolate), allocator_(isolate->allocator_) {

}