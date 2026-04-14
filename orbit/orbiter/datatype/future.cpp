// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <orbit/orbiter/fiber.h>
#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/function.h>

#include <orbit/orbiter/datatype/future.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool FutureDtor(Future *self) {
    self->cv.~condition_variable();
    self->mutex.~mutex();
    self->waiters.~FiberQueue();

    return true;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_METHOD(future_is_done, is_done,
               R"DOC(
@brief Return true when the future has been resolved or rejected.

A pending future returns false.  Once done, the state is permanent:
a future cannot transition back to PENDING.

@return true when the future is resolved/rejected, false otherwise.

@example
    let f = async_func()
    f.is_done()         // false
    f.is_done()         // true
)DOC", 1, false, false) {
    const auto *self = (const Future *) argv[0];

    return HOObject((OObject *) BOOL_TO_OBOOL(self->state != FutureState::PENDING));
}

constexpr FunctionDef future_methods[] = {
    future_is_done,

    FUNCTIONDEF_SENTINEL
};

bool orbiter::datatype::FutureAsyncAwait(Future *future) {
    std::unique_lock _(future->mutex);

    if (future->state != FutureState::PENDING)
        return false;

    future->waiters.Enqueue(Fiber::Current());

    return true;
}

bool orbiter::datatype::FutureTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) FutureDtor;

    return TIPropertyAdd(self, future_methods, PropertyFlag::IS_PUBLIC);
}

HFuture orbiter::datatype::FutureNew(Isolate *isolate) {
    auto *future = MakeObject<Future>(isolate, InstanceType::FUTURE);
    if (future != nullptr) {
        future->state = FutureState::PENDING;

        new(&future->mutex)std::mutex;
        new(&future->cv)std::condition_variable;
        new(&future->waiters)FiberQueue<false>();

        future->result = nullptr;
    }

    O_GC_TRACK_RETURN(isolate, future, false);
}

HOType orbiter::datatype::FutureTypeInit(Isolate *isolate) {
    auto future = MakeType(isolate,
                           "Future",
                           InstanceType::FUTURE,
                           (sizeof(Future) - sizeof(OObject)) - sizeof(void *),
                           1,
                           1);
    return future;
}

void orbiter::datatype::FutureAwait(Future *future) {
    const auto *isolate = O_GET_ISOLATE(future);

    // Not an active mutator while blocked waiting - inform GC we're leaving managed region
    isolate->gc->LeaveManagedRegion();

    std::unique_lock lock(future->mutex);

    future->cv.wait(lock, [future]() {
        return future->state != FutureState::PENDING;
    });

    // Thread is now an active mutator again - re-enter managed region for GC
    isolate->gc->EnterManagedRegion();
}

void orbiter::datatype::FutureReject(Future *future, OObject *result) {
    auto *orbiter = Orbiter::GetInstance();
    assert(orbiter != nullptr);

    std::unique_lock lock(future->mutex);

    future->result = result;
    future->state = FutureState::REJECTED;

    orbiter->PushFiber(future->waiters);

    lock.unlock();

    future->cv.notify_all();
}

void orbiter::datatype::FutureResolve(Future *future, OObject *result) {
    auto *orbiter = Orbiter::GetInstance();
    assert(orbiter != nullptr);

    std::unique_lock lock(future->mutex);

    future->result = result;
    future->state = FutureState::RESOLVED;

    orbiter->PushFiber(future->waiters);

    lock.unlock();

    future->cv.notify_all();
}
