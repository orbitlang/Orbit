// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#ifndef ORBIT_ORBITER_DATATYPE_CHAN_H_
#define ORBIT_ORBITER_DATATYPE_CHAN_H_

#include <orbit/orbiter/datatype/oobject.h>

#include <orbit/orbiter/sync/asyncrwlock.h>

namespace orbiter::datatype {
    /**
     * @brief Outcome of a Channel send operation.
     *
     * SENT     — value was deposited in the buffer (or directly handed off to a
     *            receiver in unbuffered mode); caller continues normally.
     * BLOCKED  — the calling fiber was enqueued on the channel's sender queue;
     *            the caller (typically an opcode handler) must transition the
     *            fiber to FiberState::SUSPENDED and yield. When the fiber wakes
     *            up the same send operation should be retried — wakeup means
     *            "a slot may be free now", not "the value is delivered".
     * FULL     — try-variant only: the channel is open but no slot is available
     *            without blocking; caller must not enqueue.
     */
    enum class ChannelSendStatus : U8 {
        SENT,
        BLOCKED,
        FULL,
    };

    /**
     * @brief Outcome of a Channel recv operation.
     *
     * DELIVERED — a value was extracted into the out parameter.
     * BLOCKED   — calling fiber was enqueued on the receiver queue (see SendStatus).
     * CLOSED    — the channel is closed and the buffer is empty; the canonical
     *             user-facing answer is `(nil, false)`. The out parameter is set
     *             to nil-object.
     * EMPTY     — try-variant only: channel is open but no value is currently
     *             available; caller must not enqueue.
     */
    enum class ChannelRecvStatus : U8 {
        DELIVERED,
        BLOCKED,
        CLOSED,
        EMPTY,
    };

    class RingBuffer {
    public:
        /// Ring buffer; size == @ref capacity. Always non-null after construction.
        OObject **buffer = nullptr;

        /// Physical buffer length. For unbuffered channels this is 1 (the hand-off slot);
        /// the user-facing capacity is reported as 0 — see `is_unbuffered`.
        MSize capacity = 0;

        /// Number of currently-occupied slots in @ref buffer.
        MSize length = 0;

        /// Index of the next slot to read.
        MSize head = 0;

        explicit RingBuffer(OObject **buffer, const MSize capacity) noexcept : buffer(buffer), capacity(capacity) {
        }

        OObject *Pop() noexcept {
            auto *value = this->buffer[this->head];
            this->buffer[this->head] = nullptr; // help GC: drop reference
            this->head = (this->head + 1) % this->capacity;
            this->length -= 1;

            return value;
        }

        void Push(OObject *value) noexcept {
            const auto tail = (this->head + this->length) % this->capacity;
            this->buffer[tail] = value;
            this->length += 1;
        }
    };

    /**
     * @brief A Go-style channel for cooperative fiber-to-fiber communication.
     *
     * A Channel is a synchronisation primitive that carries OObject values
     * between fibers. It supports two modes, selected at construction:
     *
     *   - **buffered** (capacity > 0): a ring buffer of @p capacity slots.
     *     Send blocks only when the buffer is full; recv blocks only when it
     *     is empty.
     *   - **unbuffered** (capacity == 0): no logical buffer; a send only
     *     completes when a receiver is already waiting (and vice-versa). The
     *     struct still allocates a single physical slot for hand-off; see
     *     `is_unbuffered` and the implementation in chan.cpp.
     *
     * Concurrency model: the struct is protected by a single AsyncRWLock. All
     * critical sections are short and never allocate, never call back into
     * the GC, and never block on an OS primitive — fibers that need to wait
     * are enqueued in `senders` / `receivers` and the OS thread is free to
     * pick another runnable fiber.
     *
     * Lifecycle:
     *
     *   - `close()` may be called at most once; subsequent close raises a
     *     ValueError. Send on a closed channel raises a ValueError. Recv on
     *     a closed-and-empty channel returns `(nil, false)` (no panic).
     *   - Closing wakes every queued sender (which will then panic on retry)
     *     and every queued receiver (which will see the close and possibly
     *     drain remaining buffered values before returning CLOSED).
     */
    struct Channel {
        OROBJ_HEAD;

        sync::AsyncRWLock lock;

        RingBuffer buffer;

        /// True iff the channel was constructed with logical capacity 0.
        /// Immutable after construction.
        bool is_unbuffered;

        /// True once `ChannelClose` has run successfully.
        bool closed;

        /// Fibers blocked inside `ChannelSend` (buffer full, or unbuffered
        /// without a waiting receiver).
        FiberQueue<false> senders;

        /// Fibers blocked inside `ChannelRecv` (buffer empty, channel open).
        FiberQueue<false> receivers;
    };

    using HChannel = Handle<Channel>;

    /**
     * @brief Close the channel.
     *
     * Marks the channel as closed and drains every queued waiter back to the
     * scheduler — pending senders will see the close on retry and panic;
     * pending receivers will either drain remaining buffered values or
     * observe the close.
     *
     * It is a programming error to close a channel twice; the second close
     * raises a ValueError on @p isolate and the function returns false.
     *
     * @param isolate Isolate used for error reporting.
     * @param channel Target channel.
     *
     * @return true on success, false if the channel was already closed
     *         (panic state set).
     */
    bool ChannelClose(Isolate *isolate, Channel *channel);

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
    bool ChannelTypeSetup(TypeInfo *self);

    /**
     * @brief Blocking send.
     *
     * Tries to deposit @p value (handing it directly to a waiting receiver
     * when one exists). If neither path is available the calling fiber is
     * enqueued on @ref Channel::senders and @p out_status is set to BLOCKED;
     * the caller is responsible for transitioning the fiber to SUSPENDED and
     * re-executing the same operation when the fiber resumes.
     *
     * Sending on a closed channel raises a ValueError on @p isolate and
     * returns false.
     *
     * @param isolate    Isolate used for error reporting.
     * @param channel    Target channel.
     * @param value      Value to send (must not be null; use nil-object for
     *                   "no value" semantics).
     * @param out_status SENT or BLOCKED on success — undefined on failure.
     *
     * @return true on success, false if a panic was set (send on closed).
     */
    bool ChannelSend(Isolate *isolate, Channel *channel, OObject *value, ChannelSendStatus &out_status);

    /**
     * @brief Non-blocking send.
     *
     * Same contract as @ref ChannelSend but never enqueues: returns FULL
     * instead of BLOCKED when no slot is available.
     *
     * @return true on success, false if a panic was set (send on closed).
     */
    bool ChannelTrySend(Isolate *isolate, Channel *channel, OObject *value, ChannelSendStatus &out_status);

    /**
     * @brief Returns true if the channel has been closed.
     *
     * @note Inherently racy — see @ref ChannelLen.
     */
    bool ChannelIsClosed(Channel *channel);

    /**
     * @brief Blocking recv.
     *
     * Pops a value from the buffer and writes it to @p out_value. If no
     * value is available the fiber is enqueued and BLOCKED is returned;
     * the caller must suspend the fiber and re-execute on resume. Never
     * panics.
     *
     * @param channel    Target channel.
     * @param out_value  Receives the extracted value on DELIVERED. Set to
     *                   the isolate's nil-object on CLOSED. Untouched on
     *                   BLOCKED.
     *
     * @return DELIVERED, BLOCKED or CLOSED.
     */
    ChannelRecvStatus ChannelRecv(Channel *channel, OObject *&out_value);

    /**
     * @brief Non-blocking recv.
     *
     * Same as @ref ChannelRecv but returns EMPTY instead of BLOCKED when
     * no value is currently available on an open channel.
     *
     * @return DELIVERED, EMPTY or CLOSED.
     */
    ChannelRecvStatus ChannelTryRecv(Channel *channel, OObject *&out_value);

    /**
     * @brief Construct a new channel.
     *
     * @param isolate  Owning isolate.
     * @param capacity Logical capacity. 0 → unbuffered (rendezvous);
     *                 N > 0 → buffered ring of N slots.
     *
     * @return Handle to the new Channel, or empty on allocation failure.
     */
    HChannel ChannelNew(Isolate *isolate, MSize capacity);

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
    HOType ChannelTypeInit(Isolate *isolate);

    /**
     * @brief Snapshot of the current occupied buffer length.
     *
     * @note Inherently racy — the value may already be stale by the time
     *       the caller reads the return value. Useful for diagnostics, not
     *       for synchronisation.
     */
    MSize ChannelLen(Channel *channel);

    /**
     * @brief Logical capacity (0 for unbuffered, N for buffered).
     */
    MSize ChannelCap(const Channel *channel);
}

#endif // !ORBIT_ORBITER_DATATYPE_CHAN_H_
