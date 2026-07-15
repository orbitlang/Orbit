// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0

#include <cassert>
#include <shared_mutex>

#include <orbit/orbiter/runtime.h>

#include <orbit/orbiter/datatype/error.h>
#include <orbit/orbiter/datatype/errors.h>
#include <orbit/orbiter/datatype/function.h>
#include <orbit/orbiter/datatype/number.h>
#include <orbit/orbiter/datatype/pcheck.h>
#include <orbit/orbiter/datatype/result.h>

#include <orbit/orbiter/datatype/chan.h>

using namespace orbiter::datatype;

// *********************************************************************************************************************
// INTERNAL
// *********************************************************************************************************************

bool ChannelDtor(Channel *self) {
    const orbiter::memory::IsolateAllocator allocator(O_GET_TYPE(self)->isolate);

    allocator.free(self->buffer.buffer);

    self->senders.~FiberQueue();
    self->receivers.~FiberQueue();
    self->lock.~AsyncRWLock();

    return true;
}


bool ChannelSendCommon(orbiter::Isolate *isolate, Channel *channel, OObject *value, const bool can_block,
                       ChannelSendStatus &out_status) {
    auto *orbiter = orbiter::Orbiter::GetInstance();
    assert(orbiter != nullptr);

    std::unique_lock _(channel->lock);

    if (channel->closed) {
        ErrorSet(isolate,
                 ValueError::Details[ValueError::Reason::ID],
                 nullptr,
                 "send on closed channel");

        return false;
    }

    if (!channel->receivers.IsEmpty()) {
        // Fast path: a receiver is already waiting. For unbuffered
        // channels this is the ONLY non-blocking success path; for
        // buffered channels it's just a free shortcut.
        //
        // When receivers is non-empty the buffer is necessarily
        // empty (a non-empty buffer would have woken at least one
        // of them), so RingPush cannot overflow.

        channel->buffer.Push(value);

        orbiter->PushFiber(channel->receivers.Dequeue());

        out_status = ChannelSendStatus::SENT;

        return true;
    }

    if (!channel->is_unbuffered && channel->buffer.length < channel->buffer.capacity) {
        channel->buffer.Push(value);

        out_status = ChannelSendStatus::SENT;

        return true;
    }

    if (!can_block) {
        out_status = ChannelSendStatus::FULL;

        return true;
    }

    // Enqueue the current fiber. The opcode that called us is
    // expected to set the fiber state to SUSPENDED on BLOCKED
    // and re-execute the same send when the fiber resumes — the
    // value is NOT remembered by the channel, the retry will
    // redo the attempt with whatever state the channel has at
    // wakeup.
    channel->senders.Enqueue(orbiter::Fiber::Current());
    out_status = ChannelSendStatus::BLOCKED;

    return true;
}

ChannelRecvStatus ChannelRecvCommon(Channel *channel, const bool can_block, OObject *&out_value) {
    auto *orbiter = orbiter::Orbiter::GetInstance();
    assert(orbiter != nullptr);

    std::unique_lock _(channel->lock);

    // Drain order matters: even on a closed channel, queued senders
    // (and any buffer they may have produced before close) must be
    // delivered before signalling CLOSED. Buffer-first handles both
    // cases uniformly.
    if (channel->buffer.length > 0) {
        out_value = channel->buffer.Pop();

        // A slot just freed up — wake one queued sender, if any. This
        // applies to buffered channels (a sender was waiting on a full
        // buffer); for unbuffered the senders queue is normally empty
        // here because senders only block when no receiver is waiting,
        // not when the slot is occupied by a previous transit.
        if (!channel->senders.IsEmpty())
            orbiter->PushFiber(channel->senders.Dequeue());

        return ChannelRecvStatus::DELIVERED;
    }

    // Buffer empty.
    if (channel->closed) {
        out_value = (OObject *) kOddBallNIL;

        return ChannelRecvStatus::CLOSED;
    }

    if (!can_block) {
        out_value = (OObject *) kOddBallNIL;

        return ChannelRecvStatus::EMPTY;
    }

    channel->receivers.Enqueue(orbiter::Fiber::Current());

    return ChannelRecvStatus::BLOCKED;
}

void ChannelTrace(const Channel *self, const GCTraceCallback callback, const MSize epoch) {
    // Walk the ring from `head` for `length` elements; everything else in the buffer is dead storage.
    for (MSize i = 0; i < self->buffer.length; i++) {
        const auto idx = (self->buffer.head + i) % self->buffer.capacity;
        const auto obj = self->buffer.buffer[idx];

        if (O_IS_OBJECT(obj))
            callback(obj, epoch);
    }
}

// *********************************************************************************************************************
// TYPE OPS — CONVERSION
// *********************************************************************************************************************

/// Channels are unique handles to a synchronisation primitive — identity equality only.
static bool ChannelEqual(const OObject *left, const OObject *right, bool &out) {
    out = left == right;

    return true;
}

/// Truthy while open, falsy once closed. The buffer length is intentionally
/// not part of the truthiness — `len() == 0` is best read explicitly.
static bool ChannelToBool(const Channel *self) {
    return !self->closed;
}

static OObject *ChannelToString(orbiter::Isolate *isolate, const Channel *self) {
    const char *state = self->closed ? "closed" : "open";
    const auto cap = self->is_unbuffered ? 0u : self->buffer.capacity;

    HORString s = ORStringFormat(isolate,
                                 "channel <cap=%llu, len=%llu, %s> at %p",
                                 cap,
                                 self->buffer.length,
                                 state,
                                 self);

    return s ? (OObject *) s.get() : nullptr;
}

// *********************************************************************************************************************
// RUNTIME METHODS
// *********************************************************************************************************************

RUNTIME_FUNCTION(channel_create, create,
                 R"DOC(
@brief Create a new channel.

`capacity` selects the channel mode:
  - 0 (or omitted) → unbuffered: send and recv synchronise pairwise.
  - N > 0          → buffered: ring of N slots; send blocks only when full,
                     recv blocks only when empty.

@param capacity?  Logical capacity. Defaults to 0 (unbuffered).

@return A new Chan instance.

@panic TypeError   When `capacity` is not an Int.
@panic ValueError  When `capacity` is negative.

@see close, try_send, try_recv

@example
    let unbuf = Chan.create()       // unbuffered
    let buf   = Chan.create(4)      // buffered, capacity 4
)DOC", 0, "capacity", false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("capacity", true, InstanceType::NUMBER));
    PCHECK_CHECK(params);

    MSize capacity = 0;
    if (!O_IS_SENTINEL(argv[0])) {
        IntegerUnderlying raw;

        if (!NumberExtract(argv[0], raw))
            return {};

        if (raw < 0) {
            ErrorSet(O_GET_ISOLATE(_func),
                     ValueError::Details[ValueError::Reason::ID],
                     nullptr,
                     "channel capacity cannot be negative");

            return {};
        }

        capacity = (MSize) raw;
    }

    const auto chan = ChannelNew(O_GET_ISOLATE(_func), capacity);
    if (!chan)
        return {};

    return HOObject((OObject *) chan.get());
}

RUNTIME_METHOD(channel_capacity, capacity,
               R"DOC(
@brief Return the logical capacity of the channel.

Returns 0 for unbuffered channels and N for buffered ones. Immutable for the
lifetime of the channel.

@return A non-negative Int.

@see length

@example
    Chan().capacity()    // 0
    Chan(4).capacity()   // 4
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::CHANNEL));
    PCHECK_CHECK(params);

    const auto *self = (Channel *) argv[0];

    const auto n = UIntNew(O_GET_TYPE(self)->isolate, ChannelCap(self));
    if (!n)
        return {};

    return HOObject((OObject *) n.get());
}

RUNTIME_METHOD(channel_close, close,
               R"DOC(
@brief Close the channel.

After close, every pending sender will panic on its next attempt and every
pending receiver will either drain the remaining buffered values or observe
the close.

It is a programming error to close a channel twice.

@panic ValueError  When the channel is already closed.

@see is_closed

@example
    let ch = Chan(1)
    ch.try_send(1)
    ch.close()
    ch.try_recv()    // Result(1, true)
    ch.try_recv()    // Result(nil, false)
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::CHANNEL));
    PCHECK_CHECK(params);

    auto *self = (Channel *) argv[0];

    if (!ChannelClose(O_GET_TYPE(self)->isolate, self))
        return {};

    return HOObject(kOddBallNIL);
}

RUNTIME_METHOD(channel_is_closed, is_closed,
               R"DOC(
@brief Return true if the channel has been closed.

The result is inherently racy with concurrent `close()` from another fiber;
useful for diagnostics and best-effort logic, not for synchronisation.

@return true if the channel is closed, false otherwise.

@see close

@example
    let ch = Chan()
    ch.is_closed()   // false
    ch.close()
    ch.is_closed()   // true
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::CHANNEL));
    PCHECK_CHECK(params);

    auto *self = (Channel *) argv[0];

    return HOObject((OObject *) BOOL_TO_OBOOL(ChannelIsClosed(self)));
}

RUNTIME_METHOD(channel_length, length,
               R"DOC(
@brief Return the number of values currently sitting in the buffer.

For an unbuffered channel this is always 0 outside of a transient hand-off.

@return A non-negative Int.

@see capacity

@example
    let ch = Chan(4)
    ch.try_send(1)
    ch.try_send(2)
    ch.length()      // 2
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::CHANNEL));
    PCHECK_CHECK(params);

    auto *self = (Channel *) argv[0];

    const auto n = UIntNew(O_GET_TYPE(self)->isolate, ChannelLen(self));
    if (!n)
        return {};

    return HOObject((OObject *) n.get());
}

RUNTIME_METHOD(channel_try_send, try_send,
               R"DOC(
@brief Send a value without blocking.

Deposits @p value in the buffer or hands it off directly to a waiting
receiver. If the channel is full and no receiver is waiting, the call
returns false instead of blocking.

@param value  The value to send.

@return true if the value was accepted, false if the channel is full.

@panic ValueError  When the channel is closed.

@see try_recv

@example
    let ch = Chan(1)
    ch.try_send(1)   // true
    ch.try_send(2)   // false — buffer full
)DOC", 2, nullptr, false, false) {
    PCHECK_ENTRIES(params,
                   PCHECK_DEF("self", false, InstanceType::CHANNEL),
                   PCHECK_DEF("value", true));
    PCHECK_CHECK(params);

    auto *self = (Channel *) argv[0];

    ChannelSendStatus status;
    if (!ChannelTrySend(O_GET_TYPE(self)->isolate, self, argv[1], status))
        return {};

    return HOObject((OObject *) BOOL_TO_OBOOL(status == ChannelSendStatus::SENT));
}

RUNTIME_METHOD(channel_try_recv, try_recv,
               R"DOC(
@brief Receive a value without blocking.

Returns a Result describing the outcome:
  - `Result(value, true)`  — a value was extracted from the buffer.
  - `Result(nil,   false)` — no value was available, either because the
                             channel is open and empty, or because it is
                             closed and drained.

Use `is_closed()` to disambiguate the two `false` cases when needed.

@return A Result wrapping the received value and a success flag.

@see try_send, is_closed

@example
    let ch = Chan(1)
    ch.try_recv()    // Result(nil, false)
    ch.try_send(1)
    ch.try_recv()    // Result(1, true)
)DOC", 1, nullptr, false, false) {
    PCHECK_ENTRIES(params, PCHECK_DEF("self", false, InstanceType::CHANNEL));
    PCHECK_CHECK(params);

    auto *self = (Channel *) argv[0];
    auto *isolate = O_GET_TYPE(self)->isolate;

    OObject *value = nullptr;
    const auto status = ChannelTryRecv(self, value);

    return HOObject(ResultNew(isolate,
                              status == ChannelRecvStatus::DELIVERED ? value : (OObject *) kOddBallNIL,
                              status == ChannelRecvStatus::DELIVERED));
}

constexpr FunctionDef channel_methods[] = {
    channel_create,

    channel_capacity,
    channel_close,
    channel_is_closed,
    channel_length,
    channel_try_send,
    channel_try_recv,

    FUNCTIONDEF_SENTINEL
};

// *********************************************************************************************************************
// PUBLIC API
// *********************************************************************************************************************

bool orbiter::datatype::ChannelClose(Isolate *isolate, Channel *channel) {
    auto *orbiter = Orbiter::GetInstance();
    assert(orbiter != nullptr);

    std::unique_lock _(channel->lock);

    if (!channel->closed) {
        channel->closed = true;

        orbiter->PushFiber(channel->receivers);
        orbiter->PushFiber(channel->senders);

        return true;
    }

    ErrorSet(isolate,
             ValueError::Details[ValueError::Reason::ID],
             nullptr,
             "close of closed channel");

    return false;
}

bool orbiter::datatype::ChannelTypeSetup(TypeInfo *self) {
    self->dtor = (DtorFn) ChannelDtor;
    self->trace = (TraceFn) ChannelTrace;

    auto &ops = ((TypeInfoOps *) self)->ops;

    ops.equal = ChannelEqual;
    ops.to_bool = (ToBoolFn) ChannelToBool;
    ops.to_string = (ToStrFn) ChannelToString;

    if (!TIPropertyAdd(self, channel_methods, PropertyFlag::IS_PUBLIC))
        return false;

    const auto ctor = FunctionFromDef(self, channel_create);
    if (!ctor)
        return false;

    self->ctor = (OObject *) ctor.get();

    return true;
}

bool orbiter::datatype::ChannelSend(Isolate *isolate, Channel *channel, OObject *value, ChannelSendStatus &out_status) {
    return ChannelSendCommon(isolate, channel, value, true, out_status);
}

bool orbiter::datatype::ChannelTrySend(Isolate *isolate, Channel *channel, OObject *value,
                                       ChannelSendStatus &out_status) {
    return ChannelSendCommon(isolate, channel, value, false, out_status);
}

bool orbiter::datatype::ChannelIsClosed(Channel *channel) {
    std::shared_lock _(channel->lock);

    return channel->closed;
}

ChannelRecvStatus orbiter::datatype::ChannelRecv(Channel *channel, OObject *&out_value) {
    return ChannelRecvCommon(channel, true, out_value);
}

ChannelRecvStatus orbiter::datatype::ChannelTryRecv(Channel *channel, OObject *&out_value) {
    return ChannelRecvCommon(channel, false, out_value);
}

HChannel orbiter::datatype::ChannelNew(Isolate *isolate, const MSize capacity) {
    auto *channel = MakeObject<Channel>(isolate, InstanceType::CHANNEL);
    if (channel == nullptr)
        return {};

    memory::IsolateAllocator allocator(isolate);

    // Unbuffered channels still need one physical slot for hand-off; the
    // logical 0-capacity is exposed via `is_unbuffered` and `ChannelCap`.
    const MSize physical = capacity == 0 ? 1 : capacity;

    auto **buffer = allocator.calloc<OObject *>(physical * sizeof(void *));
    if (buffer == nullptr) {
        isolate->gc->RawFree((OObject *) channel, false);

        return {};
    }

    channel->is_unbuffered = capacity == 0;
    channel->closed = false;

    new(&channel->lock) sync::AsyncRWLock();
    new(&channel->buffer)RingBuffer(buffer, physical);
    new(&channel->senders) FiberQueue<false>();
    new(&channel->receivers) FiberQueue<false>();

    O_GC_TRACK_RETURN(isolate, channel, true);
}

HOType orbiter::datatype::ChannelTypeInit(Isolate *isolate) {
    auto chan = MakeType(isolate, "Chan", InstanceType::CHANNEL, sizeof(Channel) - sizeof(OObject), 7, 0);
    return chan;
}

MSize orbiter::datatype::ChannelLen(Channel *channel) {
    std::shared_lock _(channel->lock);

    return channel->buffer.length;
}

MSize orbiter::datatype::ChannelCap(const Channel *channel) {
    return channel->is_unbuffered ? 0 : channel->buffer.capacity;
}
