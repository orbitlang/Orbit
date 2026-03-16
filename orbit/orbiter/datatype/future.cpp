// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/datatype/future.h>

#include <orbit/orbiter/runtime.h>

using namespace orbiter::datatype;

bool orbiter::datatype::FutureAsyncAwait(Future *future) {
    std::unique_lock _(future->mutex);

    if (future->state != FutureState::PENDING)
        return false;

    future->waiters.Enqueue(Fiber::Current());

    return true;
}

bool orbiter::datatype::FutureTypeSetup(TypeInfo *self) {
    return true;
}

HFuture orbiter::datatype::FutureNew(Isolate *isolate) {
    auto *future = MakeObject<Future>(isolate, InstanceType::FUTURE);
    if (future != nullptr) {
        future->state = FutureState::PENDING;

        new(&future->mutex)std::mutex;
        new(&future->cv)std::condition_variable;
        new(&future->waiters)FiberQueue<false>();
    }

    O_GC_TRACK_RETURN(isolate, future, false);
}

HOType orbiter::datatype::FutureTypeInit(Isolate *isolate) {
    auto rawptr = MakeType(isolate, InstanceType::FUTURE, sizeof(Future) - sizeof(OObject), 0, 0);
    return rawptr;
}

void orbiter::datatype::FutureReject(Future *future, OObject *result) {
    auto *orbiter = Orbiter::GetInstance();
    assert(orbiter != nullptr);

    std::unique_lock lock(future->mutex);

    future->result = O_INCREF(result);
    future->state = FutureState::REJECTED;

    orbiter->PushFiber(future->waiters);

    lock.unlock();

    future->cv.notify_all();
}

void orbiter::datatype::FutureResolve(Future *future, OObject *result) {
    auto *orbiter = Orbiter::GetInstance();
    assert(orbiter != nullptr);

    std::unique_lock lock(future->mutex);

    future->result = O_INCREF(result);
    future->state = FutureState::RESOLVED;

    orbiter->PushFiber(future->waiters);

    lock.unlock();

    future->cv.notify_all();
}

void orbiter::datatype::FutureAwait(Future *future) {
    std::unique_lock lock(future->mutex);

    future->cv.wait(lock, [future]() {
        return future->state != FutureState::PENDING;
    });
}
