// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_FUTURE_H_
#define ORBIT_ORBITER_DATATYPE_FUTURE_H_

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/fqueue.h>

namespace orbiter::datatype {
    enum class FutureState : U8 {
        PENDING,
        RESOLVED,
        REJECTED
    };

    struct Future {
        OROBJ_HEAD;

        std::mutex mutex;
        std::condition_variable cv;

        FiberQueue<false> waiters;

        FutureState state;

        OObject *result;
    };

    using HFuture = Handle<Future>;

    /**
     * @brief Awaits the completion of a future and handles its pending state
     *
     * This function is used to asynchronously wait for a future to complete.
     * If the future is currently in a pending state, it queues the current fiber
     * as a waiter for the future's resolution.
     *
     * @param future Pointer to the Future object to be awaited
     *
     * @return true if the future was in a pending state and the current fiber was successfully queued,
     * false otherwise (e.g., if the future is not in a pending state).
     */
    bool FutureAsyncAwait(Future *future);

    /**
     * @brief Set up additional features and properties for the specified type
     *
     * This function enriches the previously created type with various functionalities.
     * It typically performs the following tasks:
     * - Adds default methods to the type
     * - Adds required properties to the type
     *
     * This function is called immediately after the type's Init function to complete its setup.
     *
     * @param self Pointer to TypeInfo created by %type%Init call
     *
     * @return true if setup was successful, false otherwise
     */
    bool FutureTypeSetup(TypeInfo *self);

    /**
     * @brief Creates and initializes a new Future object.
     *
     * This function allocates and sets up a Future object within the specified
     * Isolate. It initializes the Future's state to `PENDING`, constructs necessary
     * synchronization primitives (mutex, condition variable), and prepares the
     * FiberQueue for handling waiters.
     *
     * @param isolate Pointer to the Isolate in which the Future object will be created.
     *
     * @return A handle to the newly created Future object (`HFuture`), or nullptr if creation fails.
     */
    HFuture FutureNew(Isolate *isolate);

    /**
     * @brief Initialize and create the specified type
     *
     * This function creates a new TypeInfo object representing the specific type.
     * It sets up the basic structure and core properties of the type.
     *
     * @param isolate Pointer to the Isolate in which the type is being created
     *
     * @return Handle to the newly created TypeInfo for the type, or an empty handle if creation failed
     */
    HOType FutureTypeInit(Isolate *isolate);

    /**
     * @brief Blocks execution until the specified Future is resolved.
     *
     * This function waits for a Future to transition from the PENDING state
     * to a resolved state (e.g., COMPLETED or FAILED). It uses a condition
     * variable to block the current thread until the Future's state changes.
     *
     * @param future Pointer to the Future object to wait on.
     */
    void FutureAwait(Future *future);

    /**
     * @brief Mark a Future as rejected with the specified result
     *
     * This function transitions the state of a Future object to REJECTED
     * and assigns the provided result to the Future. It performs the following:
     * - Sets the Future's result to the provided object, increasing the reference count
     * - Updates the Future's state to REJECTED
     * - Notifies all waiting threads after the state is updated
     *
     * Thread synchronization is ensured using the Future's internal mutex and condition variable.
     *
     * @param future Pointer to the Future object to be rejected
     * @param result Pointer to the OObject representing the rejection result
     */
    void FutureReject(Future *future, OObject *result);

    /**
     * @brief Resolves a future object with the provided result
     *
     * This function completes the execution of a future by assigning the given result to it
     * and updating its state to indicate resolution. Once the future is resolved,
     * any threads waiting on the future are notified.
     *
     * @param future Pointer to the Future object to be resolved
     * @param result Pointer to the result object to be stored within the future
     */
    void FutureResolve(Future *future, OObject *result);
}

#endif // !ORBIT_ORBITER_DATATYPE_FUTURE_H_
