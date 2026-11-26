# vm — bug report

**Component:** `orbit/orbiter` (interpreter: trap/panic unwind, register file) · **ID prefix:** `VM`
**PoCs:** [`poc/vm/`](poc/vm/) · **Last reviewed:** 2026-11-26
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## VM-028 — A panic raised in a `finally` skips the `catch` of an enclosing `try` in the same frame, and escapes every `trap`
**Severity:** High (a handled panic escapes to the top level) · **Status:** FIXED (2026-11-26) · **Location:** `orbit/orbiter/vm.cpp:2152` (`ERROR`)

**Fix verified (live: the PoC passes 15 of 15, 5 runs of 5, where the panic escaped
its first `trap`: an outer `catch` of the same frame takes a panic raised in a
`finally` or in a spent `catch`, its own or a callee's; with an outer `finally`
only, the `trap` gets the second panic and nothing stays pending; a `sync` around
both `try`s stays held while the `catch` runs and is released once; with `ERROR`
leaving the frame once the spent context is released, the panic escapes the `trap`
again; VM-019's, VM-018's, VM-020's and VM-023's PoCs pass; `ortest` unchanged apart
from the `numbers/` suites, red until NUM-002 lands; the PoC gate fails only the
findings already open; every `stdlib` module imports; `phi-regalloc.orb` passes).**
`ERROR` walks the contexts of the frame: a spent one is released and the next one
is looked at, until one has a live handler or the frame has none left, and only
then does the unwind leave the frame:

```cpp
ERROR:
    if (fiber->IsPanicking()) {
        auto *except = ExContextGet(fiber->vm.stack.stack, regs->CP.reg);
        while (except != nullptr && except->key == regs->BP.reg) {
            if (except->catch_off == 0 && except->finally_off == 0) {
                // Release the exhausted exception context (both catch and finally consumed)
                ReleaseExceptionContext(fiber, regs);

                except = ExContextGet(fiber->vm.stack.stack, regs->CP.reg);

                continue;
            }

            ExecuteCleanupForPC(&fiber->vm, &fiber->context);

            if (except->catch_off != 0) {
                JMP_TO(except->catch_off);
                except->catch_off = 0;

                goto CATCH_FINALLY;
            }

            JMP_TO(except->finally_off);
            except->finally_off = 0;

            fiber->panic.current_->HidePanic();
            except->info.MarkHiddenPanic();

            goto CATCH_FINALLY;
        }
    }
```

The cleanup of the `sync` blocks runs once the context that takes the panic is on
top: `ExecuteCleanupForPC` keeps the blocks holding the handler of the context on
top of `CP`, and runs once per frame, so run with a spent context on top it would
release a block the outer handler runs inside.

<details><summary>Original report</summary>

```orbit
func f() {
    try {
        try {
            panic Error(@First, "first")
        } finally {
            panic Error(@Second, "second")
        }
    } catch {
        # never runs
    }
}
r := trap f()    # does not catch either: the panic reaches the top level
```

The second panic reaches `ERROR` with the inner `try` on top of `CP`, its `catch`
and `finally` both spent. `ERROR` releases that context and goes straight to
`UnwindStack`, which pops the frame without looking at the other contexts of the
frame: the enclosing `try`, whose `catch` would take the panic, is skipped. Its
context is still on top of `CP` once the frame is popped (`PopState` does not
restore `CP`), so `UnwindStack` compares a context of a dead frame with the BP of
every caller and never reaches the `trap` below it. The same with the panics
raised by callees, and from a spent `catch` instead of a `finally`.

**Found** while verifying the fix for VM-019. With the VM at HEAD the first panic
escapes instead of the second.

**PoC:** [`poc/vm/panic-from-finally.orb`](poc/vm/panic-from-finally.orb) *(confirmed
live: the first check already lets the panic through the `trap`, and the run ends
with it)*.

**Fix (suggested, verified by probe 2026-11-25, then reverted):** before the
handlers of the top context are looked at, release the contexts of this frame whose
handlers are spent, so that the next one of the same frame gets the panic; the
cleanup of the `sync` blocks then keeps the ones that hold that context's handler:

```cpp
ERROR:
    if (fiber->IsPanicking()) {
        auto *except = ExContextGet(fiber->vm.stack.stack, regs->CP.reg);

        // A context of this frame whose handlers are spent is released first: the next one
        // of the same frame may still take the panic
        while (except != nullptr && except->key == regs->BP.reg && except->catch_off == 0 && except->finally_off == 0) {
            ReleaseExceptionContext(fiber, regs);

            except = ExContextGet(fiber->vm.stack.stack, regs->CP.reg);
        }

        if (except != nullptr) {
            ...
```

With the probe the PoC passes 15 of 15, 3 runs of 3: an outer `catch` of the same
frame takes the panic from a `finally` or a spent `catch`, its own or a callee's;
with an outer `finally` only, the `trap` gets the second panic and nothing stays
pending; a `sync` around both stays held while the `catch` runs and is released
once. VM-019's, VM-018's, VM-015's and the `sync` PoCs pass, `ortest` unchanged
apart from the `numbers/` suites, `phi-regalloc.orb` passes.

</details>

---

## VM-027 — A `return` through two `try` blocks runs only the innermost `finally`
**Severity:** High (cleanup silently skipped) · **Status:** OPEN · **Location:** `orbit/orbiter/vm.cpp:2067` (`TEND`, the RETURN branch)

```orbit
func f() {
    try {
        try {
            return 7
        } finally {
            log.append("inner")
        }
    } finally {
        log.append("outer")      # never runs
    }
}
```

A `return` inside a `try` becomes a pending return, run by the `TEND` of the
innermost `try` once its `finally` is done. The RETURN branch of `TEND` runs the
deferred calls and `Return` right away. The branch for `break` and `continue` hands
the action to the enclosing `try` of the same frame and jumps to its `finally`; the
RETURN branch does not, so every `finally` beyond the innermost is skipped. The same
for a `return` from a `catch` with a `finally` around it, through a `try` with only
a `catch`, and in a loop.

**Found** while verifying the fix for VM-019. Same behavior with the VM at HEAD.

**PoC:** [`poc/vm/return-through-finally.orb`](poc/vm/return-through-finally.orb)
*(confirmed live: 7 of 25 checks fail)*.

**Fix (suggested, verified by probe 2026-11-25, then reverted):** hand the return to
the enclosing context of the same frame, as `break` does, when its `finally` has not
started yet (the IP of this `TEND` is before it). Every `try` has a `finally` block,
an empty one when none is written, so that context's own `TEND` carries the return
further out. A `return` from inside a `finally` run by a panic lets the panic go on:

```cpp
                if (action == PendingAction::RETURN) {
                    auto *outer = ExContextGet(fiber->vm.stack.stack, ctx->info.prev());

                    if (outer != nullptr && outer->key == regs->BP.reg) {
                        // The finally of an enclosing try of this frame runs first: its TEND returns
                        const auto finally = (PtrSize) code->m_code + outer->finally_off;

                        if (outer->finally_off != 0 && regs->IP.reg < finally) {
                            const auto value = ctx->ret_value;

                            ReleaseExceptionContext(fiber, regs);

                            outer->info.SetAction((U32) PendingAction::RETURN);
                            outer->ret_value = value;

                            ExecuteCleanupForJump(&fiber->vm, &fiber->context, finally);

                            JMP_TO(outer->finally_off);
                            goto BEGIN;
                        }

                        // The return leaves a finally run by a panic: the panic goes on
                        if (outer->info.HasError()) {
                            ReleaseExceptionContext(fiber, regs);
                            ReleaseExceptionContext(fiber, regs);

                            goto ERROR;
                        }
                    }

                    if (ExecDefer(fiber))
                        goto BEGIN;
                    ...
```

The `sync` blocks around the inner `try` used to be released by the `RET` that its
`TEND` ran inside them (VM-024); the jump to the outer `finally` leaves them
instead, so they are released on the way, and only those the target is not inside:
the inner `finally` still runs with them held. Next to `ExecuteCleanupForPC`:

```cpp
// Releases the sync blocks that a jump from IP to target leaves: the ones covering IP, not target
void ExecuteCleanupForJump(VMContext *vm, const FiberContext *context, const PtrSize target) {
    const auto ip = vm->regs.IP.reg;

    const auto *table = context->code->cleanup.entries;
    for (int i = context->code->cleanup.length - 1; i >= 0; i--) {
        const auto *entry = table + i;

        if (ip < (PtrSize) entry->m_start || ip >= (PtrSize) entry->m_end)
            continue;

        if (target >= (PtrSize) entry->m_start && target < (PtrSize) entry->m_end)
            continue;

        const auto *value = *((OObject **) (vm->stack.stack + (vm->regs.BP.reg + (entry->slot * sizeof(void *)))));

        switch (entry->type) {
            case OPCode::SYNC_EXIT:
                MonitorRelease(value);
                break;
            default:
                assert(false);
                break;
        }
    }
}
```

With the probe the PoC passes 25 of 25, 3 runs of 3: two and three levels, from a
`catch`, through a `try` with only a `catch`, from inside the outer `finally` (no
restart, nothing after the `return`), from a `finally` run by a panic, before the
deferred calls, in a loop; a `sync` between the two `try`s is held in the inner
`finally` and released, once, before the outer one. Without `ExecuteCleanupForJump`
VM-024's PoC fails its `return` from a `catch` inside a block. VM-019's and VM-024's
PoCs pass, `ortest` unchanged apart from the `numbers/` suites, `phi-regalloc.orb`
passes.

---

## VM-026 — `FiberQueue::Dequeue` leaves the new head linked to the fiber it removed: a thief walks past the queue, crashes or loses fibers
**Severity:** Critical (any program whose fibers run on several threads: a crash in the scheduler, or fibers lost and the program hung) · **Status:** FIXED (2026-11-24) · **Location:** `orbit/orbiter/fqueue.cpp:66` (`FiberQueue::Dequeue`)

**Fix verified (live: the PoC passes 20 runs of 20, where it failed 10 of 10 with
hangs and crashes in `FiberQueue::Steal`; the program it comes from, 8 fibers that
only compute, 20 of 20; with `fqueue.cpp` reverted the PoC hangs 10 runs of 10;
no other code walks the links: every user goes through `Dequeue`, `PushFiber` on a
whole queue included, and `Relinquish` has no caller; the gates are those of
VM-025).** `Dequeue` cuts the links when a fiber leaves the queue: the new head no
longer points at the fiber handed out, and that fiber carries no stale link into
the next queue, so the walk of `Steal` from `tail_` ends at `head_`:

```cpp
    auto *ret = this->head_;
    if (ret != nullptr) {
        this->head_ = ret->queue.prev;

        if (this->head_ == nullptr)
            this->tail_ = nullptr;
        else
            this->head_->queue.next = nullptr;

        this->count_ -= 1;

        ret->queue.next = nullptr;
        ret->queue.prev = nullptr;
    }
```

The contended `sync` PoCs of VM-022 and VM-025 no longer hang behind it.

<details><summary>Original report</summary>

```orbit
func work(n, done) {
    w := 0
    loop i := 0; i < n * 200; i++ { w = w + 1 }
    done <- w
}
loop r := 0; r < 10; r++ {
    done := Chan(capacity=8)
    loop f := 0; f < 8; f++ { spawn work(30, done) }
    loop f := 0; f < 8; f++ { <- done }      # hangs, or SIGSEGV in FiberQueue::Steal
}
```

A `FiberQueue` links its fibers from `tail_` (the newest) to `head_` (the oldest)
through `queue.next`. `Dequeue` moves `head_` to `ret->queue.prev` but leaves the
new head's `queue.next` on `ret`, the fiber it hands out, which then runs, or is
queued elsewhere. `Steal`, which an idle thread runs on another thread's queue,
walks from `tail_` through `queue.next` until `nullptr`, with no stop at `head_`:
it goes on into fibers that are not in the queue, through links other threads are
changing. It counts fibers that are not there, cuts the queue in the wrong place,
and moves fibers it does not own; when the walk ends early it finds `mid` null and
writes `mid->queue.prev` (`fqueue.cpp:124`). Seen live: `EXC_BAD_ACCESS` at
`0x170` in `FiberQueue<true>::Steal` from `StealWork` / `FindExecutable` (three
crash reports, the same frame), and runs that hang with fibers lost from every
queue. No `sync`, channel wait or shared variable is needed: fibers that compute
and send one result each are enough.

**Found** while verifying the fix for VM-022: a stress test of contended `sync`
hung, and still hung with the `sync` removed. Same behavior with the VM at HEAD.
The crash in `Steal` listed in VM-022's report has this cause as well.

**PoC:** [`poc/vm/fiber-queue-steal.orb`](poc/vm/fiber-queue-steal.orb) *(confirmed
live: 10 of 10 runs fail, 8 hang, 2 crash in `Steal`)*.

**Fix (suggested, verified by probe 2026-11-24, then reverted):** cut the links when
a fiber leaves the queue, so that a walk from `tail_` ends at `head_`:

```cpp
    auto *ret = this->head_;
    if (ret != nullptr) {
        this->head_ = ret->queue.prev;

        if (this->head_ == nullptr)
            this->tail_ = nullptr;
        else
            this->head_->queue.next = nullptr;

        ret->queue.next = nullptr;
        ret->queue.prev = nullptr;

        this->count_ -= 1;
    }
```

With the probe the PoC passes 10 runs of 10, and the program it comes from 20 of
20 (it failed 10 of 12). `Relinquish` unlinks a fiber without clearing its own
links either; no caller was seen to depend on them.

</details>

---

## VM-025 — Two fibers can be inside `sync` blocks on the same object: the monitor reads as free while it is handed over
**Severity:** High (mutual exclusion broken under contention: lost updates, silently) · **Status:** FIXED (2026-11-24) · **Location:** `orbit/orbiter/sync/monitor.cpp:55` (`Monitor::Release`)

**Fix verified (live: the PoC passes 100 runs of 100, where it failed 10 of 10 once
VM-026 was fixed, losing increments or hanging; 16 fibers running 300 contended
blocks, preempted while held, end 100 runs of 100 with the exact count and never two
fibers inside; the VM-022 PoC passes 20 of 20; with `monitor.cpp` reverted the PoC
fails 10 runs of 10, and VM-026's still passes; `ortest` unchanged apart from the
`numbers/` suites, red until NUM-002 lands; the PoC gate fails only the findings
already open; every `stdlib` module imports; `phi-regalloc.orb` passes).** `Release`
takes the first waiter before it touches `lock_id_`, and stores either that waiter
or 0: the monitor never reads as free while a fiber waits for it, so the fast path
of `Acquire` cannot take it during the handover:

```cpp
    auto *fiber = this->queue_.Dequeue();
    if (fiber != nullptr) {
        ...
        this->lock_id_.store((uintptr_t) fiber, std::memory_order_release);

        orbiter->PushFiber(fiber);
    } else
        this->lock_id_.store(0, std::memory_order_release);
```

A fiber that finds the monitor held goes the slow way, under the spinlock `Release`
holds, and queues behind the waiter; `locks` is changed only by the owner.

<details><summary>Original report</summary>

```cpp
void Monitor::Release() {
    this->SpinLock();
    this->locks -= 1;
    ...
    this->lock_id_.store(0, std::memory_order_release);    // the monitor reads as free

    auto *fiber = this->queue_.Dequeue();
    if (fiber != nullptr) {
        ...
        this->lock_id_.store((uintptr_t) fiber, std::memory_order_release);   // handed to the waiter
        orbiter->PushFiber(fiber);
    }
```

`Release` clears `lock_id_` before it looks at the queue. Between the two stores the
monitor reads as free, and `Acquire` takes a free monitor with a compare-exchange
outside the spinlock: a fiber on another thread that runs `SYNC_ENTER` in that
window gets it. `Release` then overwrites `lock_id_` with the waiter and wakes it;
the waiter's `SYNC_ENTER` finds its own id and enters as a recursive acquire. Both
fibers are inside the block, and both bump the unsigned `locks` (the fast path does
so outside the spinlock).

Live, with 16 fibers each running 300 `sync` blocks preempted while held, 4 runs of
100 (20 such rounds each) lose an increment. A probe in `Release` (a
compare-exchange from 0 to the waiter, reporting a failure) catches the race in 2
single-round runs of 40: one loses an
increment, the other overlaps without a visible loss; the other 38 have neither.
When each fiber also works between its blocks, so that entries keep arriving during
the handover (and with VM-026 fixed by probe, whose hangs come first otherwise), 17
runs of 20 lose one and 3 hang: a `locks` bumped twice at once can come out wrong,
and a monitor whose count never returns to 0 stays held. Both go away with the fix
below.

**Found** while verifying the fix for VM-022, which made contended `sync` reachable:
before it, a waiting fiber spun and corrupted the run queues first.

**PoC:** [`poc/vm/sync-handoff-race.orb`](poc/vm/sync-handoff-race.orb) *(confirmed
live, with VM-026 fixed by probe: 7 of 10 runs lose an increment, 3 hang; while
VM-026 is open its own hangs come first)*.

**Fix (suggested, verified by probe 2026-11-24, then reverted):** look at the queue
first, and store either the waiter or 0, so the monitor never reads as free while
someone waits for it:

```cpp
    auto *fiber = this->queue_.Dequeue();
    if (fiber == nullptr)
        this->lock_id_.store(0, std::memory_order_release);

    if (fiber != nullptr) {
        ...
        this->lock_id_.store((uintptr_t) fiber, std::memory_order_release);
        orbiter->PushFiber(fiber);
    }
```

A fiber that finds the monitor held then goes the slow way, under the spinlock that
`Release` holds, and queues behind the waiter. With this probe and VM-026's, the PoC
passes 10 runs of 10 and the stress test it comes from 20 of 20, with no hang; the
VM-022 PoC passes 20 of 20.

</details>

---

## VM-024 — A `return` out of a `sync` block inside a `try` keeps the monitor held
**Severity:** High (any other fiber syncing on the object blocks forever) · **Status:** FIXED (2026-11-20) · **Location:** `orbit/liftoff/ir/builder.cpp:263` (`CreateReturn`), `orbit/liftoff/ir/builder.cpp:710` (`PutSyncExit`)

**Fix verified (live: the PoC, grown to 44 checks, passes 5 runs of 5: a `return`
out of one or two blocks inside a `try`/`catch`, a `try`/`finally` or two `try`s,
with a block on each side of the `try`, ten times through a `finally`, from a catch,
a loop or an `if` inside the block; each monitor is probed twice, so a block
released twice fails; the value, from a constant or a call, is still returned and
the `finally` runs once per `return`; the controls (a `try` inside the block,
`break` and `continue` out of it) release once; with the `SYNC_EXIT`s removed 19
checks fail; `ortest` unchanged apart from the `numbers/` suites, red until NUM-002
lands; the PoC gate fails only the findings already open; every `stdlib` module
imports; `phi-regalloc.orb` passes).** `CreateReturn` releases the blocks between
the `return` and the innermost `try` before it jumps to the `try`'s end, with the
helper `break` and `continue` use, now in `Builder`:

```cpp
    if (tcf != nullptr) {
        auto *ret = this->CreatePendingReturn(s_reg);

        this->PutSyncExit(tcf);

        this->CreateJump(((JBlockBranch *) tcf)->end);

        return ret;
    }
```

```cpp
void Builder::PutSyncExit(const JBlock *block) noexcept {
    for (auto *cursor = this->context->j_chain; cursor != block; cursor = cursor->prev) {
        if (cursor->type == JBlockType::SYNC) {
            ...
            this->CreateUnaryOp(OPCode::SYNC_EXIT, value);
        }
    }
}
```

The blocks around the `try` are left alone: `TEND` runs the pending `RET` inside
their range, and `Return` releases them there. `CreateYield` calls the same helper
with `nullptr`, releasing every block of the function as before.

<details><summary>Original report</summary>

```orbit
func get() {
    try {
        sync obj {
            return 1
        }
    } catch {
    }
}
get()
# obj's monitor is still held
```

Outside a `try`, a `return` reaches `RET` with IP inside the block's cleanup range,
and `Return` releases the monitor. Inside a `try`, `CreateReturn` turns it into a
pending return and jumps to the end of the `try`; `TEND` runs the `RET` later, from
outside the range of every block the `return` left, and no `SYNC_EXIT` was emitted
for them: `break` and `continue` get theirs from `PutSyncExit`, a `return` does not.
The same through a `finally`, and through two `try`s. A `try` inside the block is
fine: its `TEND` is still inside the block's range.

**Found** while verifying a fix for VM-020. Same behavior with the VM at HEAD.

**PoC:** [`poc/vm/sync-return-in-try.orb`](poc/vm/sync-return-in-try.orb) *(confirmed
live: the three controls pass, the three cases keep the monitor)*.

**Fix (suggested, verified by probe 2026-11-19, then reverted):** release the blocks
between the `return` and the innermost `try` before the pending jump, as `break`
does, after the value is computed:

```cpp
Instruction *IRBuilder::visitReturn(const parser::Unary *unary) {
    auto *value = unary->value != nullptr ? this->visit(unary->value) : this->builder_.LoadNilValue();

    // A return inside a try becomes a pending return, run by TEND outside the sync blocks it leaves
    const auto *tcf = this->builder_.context->GetActiveContextIf(JBlockType::TCF);
    if (tcf != nullptr)
        this->PutSyncExit(tcf);

    return this->builder_.CreateReturn(value);
}
```

With the probe the PoC passes, the value is still returned, and ten shapes of
`return`, `break` and `continue` out of `sync` and `try` release their monitor once;
`ortest` unchanged apart from the `numbers/` suites, every `stdlib` module imports,
`phi-regalloc.orb` passes.

</details>

---

## VM-023 — A panic caught inside a `sync` block releases its monitor early, and the block releases it again
**Severity:** High (the rest of the block runs without the monitor, and the monitor is broken afterwards: the next fiber that leaves it keeps it) · **Status:** FIXED (2026-11-19) · **Location:** `orbit/orbiter/vm.cpp:510` (`ExecuteCleanupForPC`), `orbit/orbiter/vm.cpp:2129` (`ERROR`)

**Fix verified (live: the PoC passes 8 of 8, 5 runs of 5: a panic of the block's own
frame caught by a `catch` inside it, taken by a `finally` inside it, or raised again
by the catch, and the control from a callee, all release the monitor once; with the
filter below removed, the four "released once" checks fail again; no monitor is
released while unheld in 81 programs, counted with a probe in `Monitor::Release`;
the gates are those of VM-020).** Resolved by VM-020's fix. `ExecuteCleanupForPC`,
which `ERROR` runs before it jumps to the handler, leaves alone the blocks that hold
the handler of the frame's own exception context, and runs once per frame, so the
unwind after a spent handler does not release the same blocks again:

```cpp
    const auto *ex_ctx = ExContextGet(vm->stack.stack, vm->regs.CP.reg);
    if (ex_ctx != nullptr && ex_ctx->key == vm->regs.BP.reg) {
        ignore = (PtrSize) (context->code->m_code + ex_ctx->catch_off);

        if (ex_ctx->finally_off != 0)
            ignore = (PtrSize) (context->code->m_code + ex_ctx->finally_off);
    }
    ...
        if (ignore >= (PtrSize) entry->m_start && ignore <= (PtrSize) entry->m_end)
            continue;
```

The block's own `SYNC_EXIT`, or the unwind after the handler, then releases it once.

<details><summary>Original report</summary>

```orbit
func update() {
    sync obj {
        try {
            panic Error(@Bad, "x")
        } catch {
        }
        # obj is no longer held here
    }   # SYNC_EXIT releases it a second time
}
```

When a panic is raised in the frame that holds the handler, `ERROR` runs
`ExecuteCleanupForPC` before it jumps to the catch or the finally. The cleanup
releases every `sync` block whose range covers IP, the ones around the `try` too,
which the handler runs inside: another fiber waiting on the object enters while the
block is still running (checked live). The block then releases the monitor a second
time: its `SYNC_EXIT` after a catch; the unwind after a finally, or after a second
panic raised by the catch (with both handlers spent `ERROR` runs the cleanup, then
`UnwindStack` runs it again on the same IP). `Monitor::locks` is unsigned: the extra
release wraps it, so the next fiber that acquires and leaves the monitor finds the
count above zero and keeps it, and nobody enters again.

A panic from a callee, caught inside the block, is released correctly: there
`UnwindStack` jumps to the catch without the cleanup.

**Found** while verifying a fix for VM-020. Same behavior with the VM at HEAD.

**PoC:** [`poc/vm/sync-catch-inside.orb`](poc/vm/sync-catch-inside.orb) *(confirmed
live: the control passes, then each of the three cases releases twice: the
monitor is free right after, held for good by the next fiber that uses it)*.

**Fix (suggested, verified by probe 2026-11-16, then reverted):** leave alone the
blocks the handler resumes in. `ExecuteCleanupForPC` takes the address the handler
resumes at and skips the entries whose range covers it:

```cpp
void ExecuteCleanupForPC(const Fiber *fiber, const FiberContext *context, PtrSize resume = 0);

        // The handler resumes inside this block: it stays held
        if (resume >= (PtrSize) entry->m_start && resume < (PtrSize) entry->m_end)
            continue;
```

and `ERROR` runs it inside the two branches, with the catch or the finally address,
and not at all when both are spent (`UnwindStack` does it next):

```cpp
                if (except->catch_off != 0) {
                    ExecuteCleanupForPC(fiber, &fiber->context, (PtrSize) code->m_code + except->catch_off);
                    JMP_TO(except->catch_off);
                    ...
                if (except->finally_off != 0) {
                    ExecuteCleanupForPC(fiber, &fiber->context, (PtrSize) code->m_code + except->finally_off);
                    JMP_TO(except->finally_off);
```

With the probe the PoC passes, and so do VM-020's and VM-021's; no monitor is
released while unheld in 17 further shapes (counted with a probe in
`Monitor::Release`); the gates are unchanged.

</details>

---

## VM-022 — A fiber waiting on a contended `sync` stays runnable: it spins, queues itself again and again, and corrupts the scheduler's queues
**Severity:** High (any contended `sync`: a crash in the scheduler, a hang, a fiber entering a held block, a monitor handed to a finished fiber) · **Status:** FIXED (2026-11-24) · **Location:** `orbit/orbiter/vm.cpp:2004` (`SYNC_ENTER`)

**Fix verified (live: the PoC passes 10 runs of 10, where it failed 10 of 10 with a
crash in the scheduler, a timeout or failed checks; with fibers waiting on held
monitors for about a second, the PoC takes 0.01 s of CPU in 1.6 s; 100 runs of 16
fibers with 300 contended `sync` blocks each end with no crash and no hang (4 lose
an increment: VM-025); `ortest` unchanged apart from the `numbers/` suites, red
until NUM-002 lands; the PoC gate fails only the findings already open; every
`stdlib` module imports; `phi-regalloc.orb` passes).** `SYNC_ENTER` parks a fiber
that finds the monitor held as `SUSPENDED_RETRY`, as a blocked channel operation
does: the scheduler drops it, and the `PushFiber` in `Monitor::Release`, which hands
it the monitor first, is its only way back. It then runs `SYNC_ENTER` again, finds
its own id in the monitor and enters:

```cpp
                if (res == 0) {
                    ...
                    fiber->state = FiberState::SUSPENDED_RETRY;
                    return nullptr;
                }
```

The fiber is queued once, on the monitor only. Two defects of their own showed up
under contention and are fixed apart: the handoff could let a second fiber in
(VM-025), and the crash in `FiberQueue::Steal` listed below also happened without
`sync` (VM-026).

<details><summary>Original report</summary>

```orbit
func holder(obj) {
    sync obj { chrono.sleep(100) }
}
spawn holder(obj)
chrono.sleep(20)
spawn enter(obj)     # waits: runs again and again instead of sleeping
```

When the monitor is held, `Monitor::Acquire` puts the fiber on the monitor's queue
and `SYNC_ENTER` returns with the state `YIELDED`, the state of a preempted fiber.
The scheduler takes it as one: it runs it again as soon as nothing else is
runnable (`FindExecutable` returns `last`), or puts it back on a run queue;
`SYNC_ENTER` runs again and queues the fiber on the monitor once more, 68,900 times
in 100 ms in one trace. A fiber has one queue link (`fiber->queue`), shared by the
run queues and the monitor's queue, so every round relinks it. Seen live: a crash
in `FiberQueue::Steal` (EXC_BAD_ACCESS, from `Orbiter::StealWork`), hangs, the debug
assertion `count_ == 0` in `~FiberQueue` at exit, a fiber entering a block another
fiber is still inside, and, after the holder hands the monitor over, the waiter's
own release handing it to a stale entry of itself, finished: nobody enters again.

**Found** while verifying a fix for VM-020: a check that spawned a fiber on a held
monitor never ended. Same behavior with the VM at HEAD. The `sync` PoCs of VM-020,
VM-021 and VM-023 probe a monitor from a hook, where a contended `sync` is refused
rather than waited for, to stay clear of it.

**PoC:** [`poc/vm/sync-contended-handoff.orb`](poc/vm/sync-contended-handoff.orb)
*(confirmed live: 0 of 10 runs pass; a crash in the scheduler, a timeout, or failed
checks)*.

**Fix (direction, not tried):** park the waiting fiber with a state the scheduler
drops, as it does for `SUSPENDED_IO` and `SUSPENDED_RETRY`, so that the
`PushFiber` in `Monitor::Release` is its only way back; it then runs `SYNC_ENTER`
once more and finds the monitor handed to it.

</details>

---

## VM-021 — `sync` on a value that is not an object crashes instead of raising
**Severity:** High (crash; on an object whose acquire fails, a release of a monitor it does not hold) · **Status:** FIXED (2026-11-16) · **Location:** `orbit/liftoff/codegen.cpp:501` (the cleanup entry's `m_start`)

**Fix verified (live: the PoC passes, 3 runs of 3, with a new case: a `sync` in a
hook refused on a monitor another fiber holds leaves that monitor intact; reverting
the line crashes the PoC (exit 139) and breaks that monitor; an uncaught `sync 5`
or `sync nil` reports "TypeError: ... cannot be used as a sync target" and exits 1;
`ortest` unchanged apart from the `numbers/` suites, red until NUM-002 lands; the
PoC gate fails only the findings already open; every `stdlib` module imports;
`phi-regalloc.orb` passes).** A block's cleanup range starts after its `SYNC_ENTER`,
so a `SYNC_ENTER` that fails leaves nothing to release:

```cpp
cursor->m_start = code->m_code + ((entry.start->instr_offset + 1) * 4);
```

The example below has an empty block, which `visitSyncBlock` drops altogether: it
never crashed and raises nothing. The PoC's blocks have a body.

<details><summary>Original report</summary>

```orbit
func on(value) {
    sync value { }
}
trap on(5)       # EXC_BAD_ACCESS in MonitorRelease, exit 139
trap on(nil)     # same
```

`SYNC_ENTER` refuses a value that is not an object with a TypeError, as it should.
The unwind then runs the block's cleanup: the entry's range starts at the
`SYNC_ENTER` instruction itself (`m_start = start->instr_offset * 4`), and IP is
still on it, so `MonitorRelease` is called on the value that was never acquired
(`5` as a pointer: address `0x25`). The same happens whenever `SYNC_ENTER` fails on
an object (allocation failure, or a contended monitor inside a synchronous call):
the cleanup releases a monitor the fiber does not hold, possibly another fiber's.

**Found** while verifying the VM-017 fix: `sync` on a small integer and on nil.

**PoC:** [`poc/vm/sync-non-object.orb`](poc/vm/sync-non-object.orb) *(confirmed
live: the control passes, then the crash)*.

**Fix (suggested, verified by probe 2026-11-10, then reverted):** start the range
after the `SYNC_ENTER`, which only has to be covered once it has succeeded:

```cpp
cursor->m_start = code->m_code + ((entry.start->instr_offset + 1) * 4);
```

With the probe the PoC passes, `sync 5` at module level reports "TypeError: 'Number'
object cannot be used as a sync object" and exits 1, and the gates are unchanged.

</details>

---

## VM-020 — A monitor stays held when a panic from a callee leaves its `sync` block
**Severity:** High (any other fiber syncing on the object blocks forever) · **Status:** FIXED (2026-11-19) · **Location:** `orbit/orbiter/vm.cpp:255` (`UnwindStack`), `orbit/orbiter/vm.cpp:510` (`ExecuteCleanupForPC`)

**Fix verified (live: the PoC passes 15 of 15, 5 runs of 5: a panic from a callee,
two frames down, from nested blocks, in a recursion with a `defer`, caught by a
`catch` or `finally` inside the block or by a `try` of the same function outside
it, in a spawned fiber; a block left by a panic caught in a caller (24 caller
layouts) or by a plain `return` under a caller's `try` or `trap` (24 layouts) is
released; no monitor is released while unheld in 81 programs, counted with a probe
in `Monitor::Release`; VM-015's PoC passes 43 of 43; `ortest` unchanged apart from
the `numbers/` suites, red until NUM-002 lands; the PoC gate fails only the
findings already open; every `stdlib` module imports; `phi-regalloc.orb` passes).**
`ExecuteCleanupForPC` takes the `FiberContext` whose cleanup table it searches, runs
once per frame (it returns when IP is already at the code's `m_end`, and moves IP
there), and leaves alone the blocks that hold the handler of the frame's own
exception context, which runs inside them:

```cpp
void ExecuteCleanupForPC(VMContext *vm, const FiberContext *context) {
    const auto ip = vm->regs.IP.reg;
    if (ip >= (PtrSize) context->code->m_end)
        return;

    vm->regs.IP.reg = (PtrSize) context->code->m_end;
    ...
    const auto *ex_ctx = ExContextGet(vm->stack.stack, vm->regs.CP.reg);
    if (ex_ctx != nullptr && ex_ctx->key == vm->regs.BP.reg) {
        ignore = (PtrSize) (context->code->m_code + ex_ctx->catch_off);

        if (ex_ctx->finally_off != 0)
            ignore = (PtrSize) (context->code->m_code + ex_ctx->finally_off);
    }
    ...
        if (ignore >= (PtrSize) entry->m_start && ignore <= (PtrSize) entry->m_end)
            continue;
```

`UnwindStack` runs it on the panicking frame, then on each caller right after
`PopStateNoCtxRestore`, with the IP its call saved, once the root frame of a spawned
fiber (no code) is ruled out:

```cpp
    ExecuteCleanupForPC(&fiber->vm, context);

    while (regs->SP.reg > 0) {
        ...
        context = p_ctx;

        // This can only occur if the code is executing in a spawned fiber
        if (context->code == nullptr)
            return false;

        ExecuteCleanupForPC(&fiber->vm, context);
```

The same filter in `ERROR` keeps a block held while a `catch` or `finally` of its own
frame runs inside it, which closes VM-023; IP at `m_end` in each caller closes
VM-018. A `return` out of a block inside a `try` is VM-024, a separate defect in
the compiler.

<details><summary>Original report</summary>

```orbit
func fail() { panic Error(@Deep, "x") }
func from_callee() {
    sync obj {
        fail()
    }
}
trap from_callee()
# obj's monitor is still held: another fiber's `sync obj` never enters
```

`ExecuteCleanupForPC` finds the `sync` block to release by IP, in the current
code's cleanup table. A panic raised in the block's own frame finds it. One from
a callee does not: when `UnwindStack` moves to the caller it sets IP to the
caller's `m_end`, outside every range, and `fiber->context` still holds the
callee's code, so the wrong table is searched as well.

**Found** while verifying a fix for VM-015. Same behavior with the VM at HEAD.

**PoC:** [`poc/vm/sync-released-on-panic.orb`](poc/vm/sync-released-on-panic.orb)
*(confirmed live: the same-frame control passes, the callee check fails)*.

**Fix (suggested, not tried):** when moving to the caller, restore its
`FiberContext` and run its cleanup against the IP saved by the call, before
moving IP to `m_end`.

</details>

---

## VM-019 — A `finally` entered by a panic stops at its first native call
**Severity:** High (cleanup silently skipped) · **Status:** FIXED (2026-11-25) · **Location:** `orbit/orbiter/vm.cpp:2153` (`ERROR`, the `finally` branch), `orbit/orbiter/vm.cpp:2052` (`TEND`), `orbit/orbiter/vm.cpp:566` (`ReleaseExceptionContext`), `orbit/orbiter/vm.cpp:79` (`ExecDefer`), `orbit/orbiter/excstack.h:22` (the context's error bit), `orbit/liftoff/ir/irbuilder.cpp:1672` (`visitTrap`)

**Fix verified (live: the PoC, grown to 38 checks, passes 5 runs of 5, where 13 of
its first 25 failed: native calls, methods, property loads and stores, and calls to
Orbit functions in a `finally` run by a panic, from the same frame or a callee, and
the statements after them; a `try` that ends normally inside the block; a second
panic caught inside the block, raised through a nested `finally`, or escaping it; a
`return` or `break` out of the block; a function with a native defer called from
it; a deferred call with a `try`, run by the unwind; the block ends where it should,
the panic goes on from its `TEND`, and nothing stays pending; each piece reverted
alone fails the PoC again: `trap` compiled as a `finally` lets every panic through,
no hiding fails 8 checks, no showing on release 5, `ExecDefer` showing a panic it
did not hide 1; VM-015's and VM-018's PoCs pass; `ortest` unchanged apart from the
`numbers/` suites, red until NUM-002 lands; the PoC gate fails only the findings
already open; every `stdlib` module imports; `phi-regalloc.orb` passes).** While a
`finally` entered by a panic runs, the panic is hidden: `HidePanic` complements its
`frame`, so `IsPanicking()` and the frame check at `BEGIN` no longer see it, and the
block's calls and loads run as in a block entered normally. The context remembers
it with a bit in `info` (`prev` now shifted by three):

```cpp
                if (except->finally_off != 0) {
                    JMP_TO(except->finally_off);
                    except->finally_off = 0;

                    fiber->panic.current_->HidePanic();
                    except->info.SetError();

                    goto CATCH_FINALLY;
                }
```

The `TEND` of that context, and only that one, shows the panic again and goes on
unwinding; a `try` nested in the block has no bit and leaves it hidden:

```cpp
                if (ctx->info.HasError()) {
                    fiber->panic.current_->ShowPanic();
                    ctx->info.UnsetError();
                }

                if (fiber->IsPanicking()) {
                    ReleaseExceptionContext(fiber, regs);

                    goto ERROR;
                }
```

A context with the bit left another way (a second panic escaping the block, a
`return` or a `break`) shows the hidden panic when it is released, so a `catch` or
`trap` that takes the newer panic discards both and nothing stays pending; a
`return` or `break` in the block does not swallow the panic:

```cpp
    if (ctx->info.HasError()) {
        for (auto *panic = fiber->panic.current_; panic != nullptr; panic = panic->prev) {
            if (panic->frame < 0) {
                panic->ShowPanic();
                break;
            }
        }
    }
```

`ExecDefer` hides the pending panic around a native deferred call only when it is
not hidden already, and shows only what it hid. A `trap` is compiled with a `catch`
that leads to `STRES` and a `finally` on its `TEND`: `STRES` reads the panic it
discards, which a hidden one would not show.

Native calls report a failure through the panic they raise (`CallNative` compares
the current panic before and after the call, and pops its arguments either way), and
`LDOBJP` / `STOBJP` through the result of the load or store, rather than from
`IsPanicking()` after the instruction. With the panic hidden the PoC passes either
way; these keep a failure apart from a panic that was already pending.

<details><summary>Original report</summary>

```orbit
try {
    panic Error(@Own, "own")
} finally {
    log.append("first")     # the pending panic reads as a failure of this call
    count += 1              # never runs
}
```

The panic is still pending while the block runs, so the check after a native call
takes it for a failure of that call and the rest of the block is skipped. A call
to an Orbit function in the block is lost too, depending on its argument count:
`panic->frame` is still the callee's BP, so a frame landing exactly on it is taken
for the panicking one at `BEGIN`, and one below it sees the panic as its own. This
is VM-015's first cause, in a `finally`: setting the panic aside for the block
needs a state of its own, because the block runs in the frame the panic belongs to.

**Found** while verifying a fix for VM-015. Same behavior with the VM at HEAD.

**PoC:** [`poc/vm/finally-native-during-panic.orb`](poc/vm/finally-native-during-panic.orb)
*(confirmed live: six of eight checks fail)*.

**Fix (suggested, not tried):** a flag on the panic, set on entering the `finally`
and cleared by its `TEND`, which `IsPanicking` honors. `TEND` has to recognize its
own context (a nested `try` in the block ends with a `TEND` too), and returning
into the frame from a call in the block must not resume the unwind at `BEGIN`.

</details>

---

## VM-018 — After a panic from a callee, a `try` without a live catch resumes after the call
**Severity:** High (statements run that the panic should have skipped) · **Status:** FIXED (2026-11-19) · **Location:** `orbit/orbiter/vm.cpp:255` (`UnwindStack`), `orbit/orbiter/vm.cpp:510` (`ExecuteCleanupForPC`), `orbit/orbiter/vm.cpp:2124` (`ERROR`)

**Fix verified (live: the PoC, grown to 32 checks, passes 5 runs of 5: a `finally`
alone one and two frames down, with a `defer` in the frame, inside a `try`/`catch`
of the same frame, a catch whose callee panics again with or without a `finally`
after it, a `finally` whose callee panics, nested `try`s of one frame whose inner
catch is spent, a caller's `catch` after the callee's `finally`, and the flow
without a panic; with IP put back on the return address when the context has no
live catch, the eight "the rest does not run" checks fail again; the gates are
those of VM-020).** Resolved by VM-020's fix. `UnwindStack` runs
`ExecuteCleanupForPC` on each caller it reaches, which moves IP to the frame's
`m_end`; the handler branch sets IP only for a live catch, so a context with a
`finally` alone, or with its handlers spent, resumes at `m_end`:

```cpp
        ExecuteCleanupForPC(&fiber->vm, context);   // IP = m_end

        // Is there at least one exception handler?
        if (except != nullptr && except->key == regs->BP.reg) {
            stratum::util::MemoryCopy(&fiber->context.context, context, sizeof(FiberContext));

            if (except->catch_off != 0)
                regs->IP.reg = (PtrSize) fiber->context.code->m_code + except->catch_off;

            return true;
        }
```

The dispatch loop runs no instruction at `m_end` and falls into `ERROR`, which
treats the context as for a panic raised in the frame: it jumps to the `finally`,
consuming it, or releases the spent context and goes on unwinding. Its own
`ExecuteCleanupForPC` returns at once, IP being at `m_end` already.

<details><summary>Original report</summary>

```orbit
try {
    fail()
    rest += 1      # runs
} finally { ... }

try {
    fail()
} catch {
    fail()
    rest += 1      # runs
}
```

`UnwindStack` treats any context of the frame as a handler. It sets IP only when
`catch_off` is live: with a `finally` alone, or with a context whose handlers are
spent, it returns `true` with IP still on the instruction after the call. The
`ERROR:` path handles the same cases, one per offset, and releases an exhausted
context; this branch does not. On a build where the pending panic fails every
native call, the next call hides the defect, so the PoC counts with arithmetic.

**Found** while verifying a fix for VM-015. Same behavior with the VM at HEAD.

**PoC:** [`poc/vm/unwind-consumed-context.orb`](poc/vm/unwind-consumed-context.orb)
*(confirmed live: two of six checks fail)*.

**Fix (suggested, not tried):** mirror `ERROR:` in that branch: jump to the catch if
live, else to the `finally` (consuming it), else release the context and go on
unwinding, re-reading the next context, which may belong to the same frame.

</details>

---

## VM-017 — `sync` crashes: an object's monitor field is never initialized
**Severity:** High (crash on valid source) · **Status:** FIXED (2026-11-10) · **Location:** `orbit/orbiter/datatype/oobject.h:553` (`MakeObject`), `orbit/orbiter/datatype/oobject.cpp:164` (`MakeType`)

**Fix verified (live: the PoC passes 5 runs of 5 and 100 runs of 100 with 8 in
parallel, including its last check, 20 classes made at run time after churning and
collecting the heap; `sync` holds on 23 kinds of object — lists, strings, dicts,
sets, tuples, bytes, decimals, heap Numbers, functions, closures, modules, atoms,
results, channels, errors, generators, instances with fields, `init` and a
superclass, built-in types, classes and traits — 20 fresh processes each; `ortest`
unchanged apart from the `numbers/` suites, red until NUM-002 lands; the PoC gate
fails only the findings already open; every `stdlib` module imports;
`phi-regalloc.orb` passes).** Both places that create an object head now clear its
monitor: `MakeObject` for instances, `MakeType` for types, which it allocates with
`gc->AllocObject` directly:

```cpp
O_GET_HEAD(ret).mon_ = nullptr;   // MakeObject
O_GET_HEAD(ti).mon_ = nullptr;    // MakeType
```

`sync` on a value that is not an object still crashes: that is VM-021, a separate
defect in the block's cleanup range.

<details><summary>Original report</summary>

`MakeObject` sets `type_` and the reference count but not `mon_`, so
`MonitorAcquire` reads whatever the allocator left there and, unless that is
zero, calls `Acquire` on a garbage pointer (`this=0x500000000`, `this=0x269`:
EXC_BAD_ACCESS, exit 138 or 139, with the buffered output lost). Nothing else
writes the field, so whether `sync` survives depends on the block the object got:
a list or an empty class often does, an instance with fields does not.

```orbit
class Field {
    pub var v = 0
}
f := new Field()
sync f { }         # crash
```

**Found** while verifying a fix for VM-015. Same behavior with the VM at HEAD. No
suite exercised `sync`.

**PoC:** [`poc/vm/sync-instance-with-fields.orb`](poc/vm/sync-instance-with-fields.orb)
*(confirmed live: the two controls pass, then the crash)*.

**Fix (suggested, verified by probe 2026-11-05, then reverted):** in `MakeObject`:

```cpp
O_GET_MON(ret).store(nullptr, std::memory_order_relaxed);
```

With the probe the PoC passes 5 of 5 and the gates are unchanged. `oobject.cpp:143`
allocates TypeInfos without `MakeObject`; a type used as a `sync` object was not
checked.

</details>

---

## VM-016 — A native function reads its arguments from a VM stack that a hook it calls can reallocate
**Severity:** High (use after free: wrong values, wrong types, possible crash) · **Status:** OPEN · **Location:** `orbit/orbiter/vm.cpp:486` (`CallNative`), `orbit/orbiter/vmstack.cpp:52` (`VMStack::Grow`), users such as `orbit/orbiter/datatype/list.cpp:526` (`contains`), `:565` (`count`), `:634` (`index`), `orbit/orbiter/datatype/tuple.cpp:395` (`count`)

`CallNative` passes a native function its arguments as `argv`, a pointer
into the VM stack:

```cpp
auto **args = (OObject **) (stack->stack + (regs->SP.reg - offset));

const auto result = func->shared->func(func, args, args[argc], args[argc + 1], argc);
```

A native that compares, hashes or prints an instance calls the instance's
hook (`equal`, `compare`, `hash`, `tostr`, `repr`) through
`Orbiter::EvalSync`, which runs it on the same fiber and the same stack. When
the hook needs more stack, `VMStack::Grow` reallocates the buffer
(`allocator.realloc`) and the old one is freed, but the native still holds
`argv` into it. Every read of `argv` after the first hook is a read of freed
memory:

```orbit
func deep(n) { return n == 0 ? 0 : 1 + deep(n - 1) }

class Key {
    pub var id
    pub init(id) { self.id = id }
    pub func equal(other) {
        deep(20000)                     # grows the stack: argv is left behind
        # ...allocations that reuse the freed block...
        return self.id == other.id
    }
}

[new Key(1), new Key(2), new Key(3)].contains(new Key(3))
# AttributeError: 'Number' object has no property 'id'
```

`list.contains`, `list.count`, `list.index` and `tuple.count` read
`argv[1]` again for every element, after the previous element's `equal`.
`dict.get` reads its default (`argv[2]`) after the lookup, which calls
`hash` and `equal`; it is exposed the same way, though the PoC did not catch
it reading a reused value. Any other native that reads `argv` after calling
a hook is too. Nothing goes wrong until the freed block is reused: without
the allocations in the hook the results are right, which is how it stayed
unnoticed.

**Found** checking the audit note left with VM-009 (a native's `args` across
a callback that grows the stack). Confirmed with a probe in `list.contains`:
`argv` lies inside the stack (16 KiB at `0x74eac10010`) for the first element
and outside it from the second on, after the first `equal` moved the stack to
`0x74eb400010` (1.8 MB).

**PoC:** [`poc/vm/native-args-stack-growth.orb`](poc/vm/native-args-stack-growth.orb)
*(confirmed live: the three checks fail 10 runs out of 10 with "'Number'
object has no property 'id'"; all pass when the hook does not grow the
stack, or grows it without reusing the block)*. Each case runs in a spawned
fiber so that it starts with a small stack; spawned fibers come from a pool
that keeps a grown stack, so a fourth case, or a case moved after them, may
not grow its stack at all and pass.

**Fix (suggested, verified by probe 2026-10-30, then reverted):** never hand
a native a pointer into the VM stack. With `CallNative` copying the
arguments, and the two slots after them, into a buffer of its own
(`std::vector<OObject *> args(on_stack, on_stack + argc + 2)` in the probe;
a small fixed array on the C stack with a heap fallback avoids an allocation
per call) the PoC passes 3 runs out of 3 and in the other order,
`ortest` is 34/34, the PoC gate 65/67 with the open VM-010 and VM-015, and
`phi-regalloc.orb` passes. The values stay reachable for the GC through the
stack slots, which `realloc` copies, until `CallNative` pops them. A
stack that never moves (reserved up to its limit and committed on demand)
would remove this class of bug, VM-009's included, at a higher cost.

---

## VM-015 — A deferred call is lost when a panic unwinds its frame, and can resume its caller with the wrong code
**Severity:** High (cleanup silently skipped; the VM executes from a wrong address) · **Status:** FIXED (2026-11-08) · **Location:** `orbit/orbiter/vm.cpp:79` (`ExecDefer`), `:255` (`UnwindStack`), `:734` (`BEGIN`), `STRES`; `orbit/orbiter/fiber.h` (`IsPanicking`); `orbit/orbiter/panic.h`, `panic.cpp` (`HidePanic`/`ShowPanic`, `DiscardPanic`); `orbit/orbiter/module/builtin.cpp` (`panicking`)

**Fix verified (live: the PoC passes 43 of 43 checks, and 300 runs of it with 8 in
parallel all exit 0; the 47 further cases written to break it (27 in the first
review, 20 in the second) hold — deferred calls
that sleep, block on a channel, get preempted, consume a generator, run the GC,
`trap` or `catch` errors of their own, call functions with defers, run natives
that call hooks, raise a panic of their own; hooks with defers; defers at module
level; a stack overflow running a defer in each of 599,183 frames, 100 runs in
parallel; the code after a `try` that does not catch stays skipped, and a hook's
panic reaches its caller; `ortest` unchanged apart from the `numbers/` suites, red
until NUM-002 lands; the PoC gate fails only the findings already open; every
`stdlib` module imports; `phi-regalloc.orb` passes).** A panic is set aside while a
deferred call runs, and stays visible to the frames it belongs to:

- `panic->frame` is a signed `MSSize`. `UnwindStack` moves it to the frame being
  unwound before running that frame's defers, so the deferred call and everything
  it calls sit above it.
- `IsPanicking()` is `HasPanic() && frame >= BP`: frames at or below the panic see
  it (`TEND` re-raises, `EvalSync` reports a failing hook), frames above do not (a
  deferred function's calls, natives, property loads run normally). `BEGIN` keeps
  the exact `frame == BP` test that resumes the unwind once a deferred call
  returns. `panicking()` reports `HasPanic()`, so it is true inside a deferred call.
- A native deferred call runs at the unwound frame's own BP, so the panic is hidden
  for it: `HidePanic()` stores `~frame` (negative, frame 0 included) and
  `ShowPanic()` restores it. A panic the native raises gets the unwound frame.
- `DiscardPanic` takes a base frame and discards only the panics from that frame up,
  so a `trap` or `catch` inside a deferred call takes its own error and leaves the
  outer panic pending (a hidden panic, negative, is never discarded). `STRES` asks
  `IsPanicking()`, so a `trap` that succeeds inside a deferred call returns ok.

Point 3 of the report (the caller resuming with the callee's code) no longer runs
any code: a deferred call returning into a frame being unwound goes straight to
`ERROR` at `BEGIN`. The stale `FiberContext` it leaves is part of VM-020.

<details><summary>Original report and the reviews of earlier attempts</summary>

A `defer` must run however its frame is left. On a normal return it does;
when a panic unwinds the frame, `UnwindStack` pops the deferred call and
starts it (`ExecDefer`), but the call is lost in three ways, and in a fourth
shape it corrupts the caller:

```orbit
var log = []
func mark(t) { log.append(t) }
func fail() { panic Error(@Deep, "x") }
func f() { defer mark("f"); fail() }
trap f()          // log is still []
```

1. **The pending panic reads as a new failure.** The panic being unwound
   stays in `fiber->panic` while the deferred function runs. The VM detects a
   failed operation by looking for a pending panic (`if
   (fiber->IsPanicking()) goto ERROR` after a call, `panic.current_ !=
   nullptr` after a non-inline property load or store, `TEND`, ...), so the
   first such check in the deferred function jumps to `ERROR:` and its frame
   is unwound as if it had failed. A deferred function that calls a native
   (`log.append`, `io.print`) never gets past that call; one that only does
   arithmetic on a module variable runs (the PoC's control).
2. **The deferred frame is taken for the panicking frame.** `RaisePanic`
   records the panicking frame by its BP (`p->frame`), and `BEGIN` resumes
   unwinding when it enters a frame with that BP. `ExecDefer` places the
   deferred frame at `defer->SP + kStackPrologueOffset`. When the panic came
   from a callee called with no arguments, the callee's frame sat at exactly
   that BP, so the deferred function is sent to `ERROR:` before its first
   instruction.
3. **The caller resumes with the callee's code.** When `UnwindStack` moves to
   the caller it uses `PopStateNoCtxRestore` and copies the caller's
   `FiberContext` only when a handler is found, so `fiber->context` is still
   the callee's when the caller's defers run. `ExecDefer` saves that context
   with `PushState`; when the deferred function returns, `PopState` puts the
   callee's code back under the caller's BP, with IP set to the caller's
   `m_end`. `BEGIN` does not recognise the frame (its BP is not
   `panic->frame`), so the loop executes the callee's code from the caller's
   address: in the debug build it aborts on `assert(false)` for an unknown
   opcode, in a release build it runs whatever words are there. Reached when
   the deferred function survives points 1 and 2 (it makes no call, and the
   callee took arguments, so the BPs differ):

   ```orbit
   var count = 0
   func bump() { count += 1 }
   func fail_with(a, b) { panic Error(@Deep, "x") }
   func f() { defer bump(); fail_with(1, 2) }
   trap f()          // Assertion failed: (false), function eval, file vm.cpp
   ```

A panic not caught anywhere loses its defers the same way, before the
process exits. The behavior is the same with the VM at HEAD (`13f0ccd`);
the VM-014 fix only touches the root frame.

**Found** while checking VM-014's fix through the panic paths: a
`defer io.print(...)` in a function that called a panicking function printed
nothing. Traced with probe prints in `ExecDefer`, `UnwindStack`, `BEGIN` and
`RET`: the deferred frame's BP equal to `panic->frame` (point 2), the
deferred function's frame unwound from inside after `log.append` (point 1),
and `BEGIN` entering the caller's BP with an IP 61,300 bytes before the code
it runs (point 3).

**PoC:** [`poc/vm/defer-during-unwind.orb`](poc/vm/defer-during-unwind.orb)
*(confirmed live on the VM before any fix: the two controls pass, six checks fail,
and the file aborts at point 3; extended 2026-11-05 to 42 checks with the cases
below)*.

**A fix reviewed, not accepted (2026-11-05).** `ExecDefer` moved `panic->frame` to
the frame being unwound and `IsPanicking()` became `HasPanic() && frame == BP`.
That fixes the three points above, but a panic raised in a deeper frame is now
invisible to every frame below it, so 10 of the 28 checks fail:

- `TEND` does not re-raise: the code after a `try` that did not catch the panic
  runs, and an outer `catch` of the same frame is missed;
- `EvalSync` reports success after a panicking hook: `str`, `equal` and `compare`
  hooks surface as a TypeError ("returned non-string") instead of their error;
- `ExecDefer` also moves the frame on a normal return inside the deferred call, so
  a deferred function that calls a function with a defer stops there;
- `panicking()` is false inside a deferred call, against its documentation.

The same approach passes all 28 checks and leaves the gates unchanged (probe,
then reverted) with four changes: `frame >= BP` in `IsPanicking` (frames below the
panic see it, deferred calls above the moved frame do not); the original
`frame == BP` test at `BEGIN`, since a handler found below must not jump to
`ERROR`; `frame` moved in `UnwindStack` right before `ExecDefer`, not inside it; and
`panicking()` on `panic.HasPanic()`. Point 3 stays as it is there (the context is
not restored), harmless on these paths because `BEGIN` goes straight to `ERROR`,
but see VM-020.

**Second review (2026-11-05), those four changes applied: still OPEN.** They hold
under sleep, a blocking channel, preemption, a generator and `gc.collect()` inside a
deferred call, a hook with a defer, module-level defers, and a stack overflow with
a defer in each of 599,183 frames. What remains is a deferred call doing what a
function normally does while the outer panic is set aside:

- a `trap` that succeeds in a deferred call returns an error and discards the outer
  panic: `STRES` tests `panic.current_ == nullptr`. The outer `trap` sees `ok`;
- a `trap` or a `catch` that takes an error raised in the deferred call discards the
  outer panic too: `DiscardPanic` empties the whole chain. If the panic was raised in
  the frame itself, that frame then resumes on its `panic` instruction, with a stale
  register ("panic expect type 'Error'");
- a native deferred call runs with `frame == BP`, so the panic is visible to it: one
  that runs a hook (`defer table.set(key, 1)`) fails and does nothing.

**Fix (suggested, verified by probe 2026-11-05, then reverted), on top of those
four changes:**

```cpp
// panic.h, Panic: a pending panic set aside for a native deferred call
bool hidden;   // false in CreatePanic and CreateOOMPanic

// panic.cpp: only what belongs to the frames from min_frame up
void PanicContainer::DiscardPanic(Panic **panic_cache, const PtrSize min_frame) const noexcept {
    ...
    while (chain != nullptr && chain->frame >= min_frame) { ... }

    *this->r_current_ = chain;
}

// fiber.cpp, Fiber::DiscardPanic
this->panic.DiscardPanic(&this->panic_cache, this->vm.regs.BP.reg);

// fiber.h, IsPanicking
const auto *current = *this->panic.r_current_;

return current != nullptr && !current->hidden && current->frame >= vm.regs.BP.reg;

// vm.cpp, STRES
if (!fiber->IsPanicking()) {

// vm.cpp, ExecDefer, the native branch
auto *pending = fiber->panic.current_;
if (pending != nullptr)
    pending->hidden = true;

CallNative(func, regs, stack, defer->argc);

if (pending != nullptr)
    pending->hidden = false;

if (fiber->panic.current_ != pending)
    fiber->panic.current_->frame = regs->BP.reg;
```

With the probe the PoC passes all 42 checks (200 runs, 8 in parallel, exit 0), the
gates are unchanged and every `stdlib` module imports. Each part is needed: taking
out any one of the `STRES` test, `min_frame`, `hidden` and the `frame` update makes
the PoC fail.

**Fix (suggested, not tried):** the three points need one design: an
unwinding that a deferred call can interrupt and resume.

- Resume by frame, not by BP: before `UnwindStack` starts a deferred call,
  set `panic->frame` to the frame being unwound (the one that owns the
  defer). Every frame the deferred call creates sits above that frame's SP,
  so none can have its BP until the deferred call has returned into it.
- Restore the caller's `FiberContext` whenever `UnwindStack` moves to a
  previous frame (`PopState` instead of `PopStateNoCtxRestore`, after the
  generator check that reads the old context), so a deferred call saves and
  restores the right code; the copy made when a handler is found becomes
  redundant.
- Hide the unwound panic while the deferred call runs, so the pending-panic
  checks see only a panic raised by the deferred call itself: for example
  detach the chain from `fiber->panic` into the frame being unwound and put it
  back when `BEGIN` resumes the unwinding there. A panic raised by the
  deferred function then has to be chained onto it (`CreatePanic` links
  `prev`, printed as "while handling"), and the detached chain has to stay
  reachable by the GC.

</details>

---

## VM-014 — The end of a module pops a frame state from below the VM stack: intermittent SIGBUS at exit
**Severity:** High (out-of-bounds read; intermittent crash after the program has run) · **Status:** FIXED (2026-10-27) · **Location:** `orbit/orbiter/vm.cpp:255` (`UnwindStack`)

**Fix verified (live: a probe log in `PopStateNoCtxRestore` for `BP <
kStackPrologueOffset` stays silent on all 89 files under `ortest/` and
`issues/poc/` (`ortest/net/` left out), on every `stdlib` import and on
uncaught and trapped panics at module level, in a function, in a `__tostr`
hook and in a spawned fiber, where the HEAD VM fires at every module end; the
two PoCs that crashed exit 0 in 1,200 runs, 8 in parallel, against 3 SIGBUS in
600 before; `ortest` 34/34, the PoC gate 64/65 with the open VM-010,
`phi-regalloc.orb` passes).** `UnwindStack` stops at the root frame before it
looks at the barrier, so a frame with `BP == 0` is never popped:

```cpp
if (ExecDefer(fiber))
    return true;

if (regs->BP.reg == 0)
    break;

if (regs->BP.reg == barrier) {
    fiber->PopState();

    return false;
}
```

The check used to come after `regs->SP.reg = regs->BP.reg`, where it was
never reached with `BP == 0`: the root frame runs with `barrier == 0`, so the
barrier branch took it first. The output of the panic paths above is the same
as with the HEAD VM, apart from the out-of-bounds reads.

<details><summary>Original report</summary>

The root frame of a fiber started by the scheduler (`eval(fiber, 0)`,
`runtime.cpp:282`) runs with `BP == 0` and has no saved state under it: no
`PushState` precedes it. `Return` knows this and handles `BP == 0` on its own
(it moves IP to `m_end`). `UnwindStack` does not:

```cpp
while (regs->SP.reg > 0) {
    ...
    if (regs->BP.reg == barrier) {
        fiber->PopState();   // BP == barrier == 0: reads below the stack base

        return false;
    }
```

The dispatch loop falls into `ERROR:` when IP reaches `m_end`, so every
module that ends with `SP > 0` (a module-level `try`, a loop, any reserved
slot; the `io` module itself when imported) takes this branch, as does an
uncaught panic at module level. `PopStateNoCtxRestore` then reads IP, BP and
SP from the three words before `stack.stack`, and `PopState` copies a
`FiberContext` from below them into `fiber->context`. Usually that memory is
mapped and the garbage goes unnoticed, because the fiber is finished; when
the stack buffer starts right after an unmapped page the read faults
(`EXC_BAD_ACCESS code=2`, the address 8 bytes below a page boundary) and the
process dies with SIGBUS (exit 138) after printing all its output.

Backtrace of the faulting (scheduler) thread, caught under lldb:

```
orbiter::Fiber::PopStateNoCtxRestore  fiber.cpp:132
orbiter::Fiber::PopState              fiber.cpp:187
UnwindStack(fiber, barrier=0)         vm.cpp:256
orbiter::eval(fiber, barrier=0)       vm.cpp:2122
orbiter::Orbiter::Scheduler           runtime.cpp:282
```

At that moment the main thread is still in `FutureAwait` (`Orbiter::Eval`)
and the I/O loop thread is idle in `Orbiter::IOLoop`: the crash is not in
the event loop's cleanup.

**Found** while checking the gates for PARSE-027: a full PoC run failed
`vm/exception-context-module-slot.orb` and `vm/method-receiver-plain-call.orb`,
which then passed 40 times out of 40 run alone. Run 300 times each with 8 in
parallel they printed `ALL TESTS PASSED` every time but exited with SIGBUS 3
times out of 600. It is a plausible cause of the single intermittent
failures recorded in the README log on 2026-10-27 (`ir-004`,
`ortest/operators/01_is.orb`, `ortest/gc/02_function_owner.orb`), not
confirmed for them.

**PoC:** none: the read only faults depending on where the stack buffer
lands, so a PoC would pass almost every time. Confirmed live with a probe log
in `PopStateNoCtxRestore` (`BP < kStackPrologueOffset`): it fires at the end
of `poc/vm/exception-context-module-slot.orb` (`BP=0 SP=48`),
`poc/vm/method-receiver-plain-call.orb` (`SP=80`) and
`ortest/operators/01_is.orb` (`SP=56`), and once more in each for the
imported `io` module (`SP=8`).

**Fix (suggested, verified by probe 2026-10-27, then reverted):** pop only a
frame that has a saved state, as `Return` does:

```cpp
if (regs->BP.reg == barrier) {
    if (regs->BP.reg > 0)
        fiber->PopState();

    return false;
}
```

With the probe the log stays silent for every file under `ortest/` and
`issues/poc/` (`ortest/net/` left out) and for every `stdlib` import, the
two PoCs above exit 0 in 1,200 runs (8 in parallel), `ortest` is 34/34, the
PoC gate 64/65 with the open VM-010, and `phi-regalloc.orb` passes.

</details>

---

## VM-013 — `CallNative` reads rest and kwargs above SP and pops only the counted arguments
**Severity:** Low (latent: no native is variadic or takes keyword arguments today) · **Status:** OPEN · **Location:** `orbit/orbiter/vm.cpp:486` (`CallNative`), callers `vm.cpp:123` (`Call`) and `vm.cpp:91` (`ExecDefer`)

`CallNative` finds the arguments of a native function from SP and the bound
count alone, and hands the two slots after them to the native as the rest
list and the kwargs dict:

```cpp
const auto offset = argc * sizeof(void *);
auto **args = (OObject **) (stack->stack + (regs->SP.reg - offset));

const auto result = func->shared->func(func, args, args[argc], args[argc + 1], argc);
...
regs->SP.reg -= offset;
```

`ArgumentBinder::Bind` lays out a call as the counted parameters followed by
the rest list and the kwargs dict, which it pushes without counting them in
`StackArgs()` (`FinalizeRestArgs`, `FinalizeKwargs`). The two layouts disagree
whenever either slot is pushed:

- with no rest list and no kwargs dict on the stack (every call today),
  `args[argc]` and `args[argc + 1]` are the two slots **above** SP: stale
  values from earlier calls, or past the end of the stack buffer when the
  stack is full;
- with them pushed (a native declared variadic or taking `*kwargs`), `args`
  starts one or two slots too high, so every argument is read shifted, and
  `SP -= offset` leaves the uncounted slots on the stack, which corrupts the
  caller's frame the way a leaked argument slot does.

Nothing triggers it now: all 215 native functions and methods are declared
with `varargs` and `kwargs` false, so `Bind` never pushes the two slots for a
native (a rest list is consumed into the declared parameters or refused, a
kwargs dict is refused with `NO_KWARGS`), and no native reads its `rest` or
`kwargs` arguments. The first native that declares either would hit it.

**Found** while checking how the rest list and the kwargs dict reach the
callee, for VM-011's `DropReceiver`.

**PoC:** none — not triggerable from Orbit while no native is variadic or takes
keyword arguments; verified by inspection (`CallNative`, `FinalizeRestArgs`,
`FinalizeKwargs`, and the `varargs`/`kwargs` flags of every `RUNTIME_FUNCTION`
and `RUNTIME_METHOD`).

**Fix (suggested, not tried):** take the region from the binder instead of
recomputing it: the arguments start at `ArgsBaseSP()`, the rest list and the
kwargs dict are passed only when the function is variadic or takes kwargs
(`nullptr` otherwise), and SP goes back to `ArgsBaseSP()`. `ExecDefer` calls
`CallNative` with a saved `argc` and also has `restorable_SP`, the same value.

---

## VM-012 — A method called without a selector does not check its receiver
**Severity:** High (reads the wrong slots of a foreign object; SIGSEGV with a non-object) · **Status:** FIXED (2026-10-22) · **Location:** `orbit/orbiter/orbcall.cpp:128` (`ArgumentBinder::CheckReceiver`), `orbit/orbiter/orbcall.cpp:204` (`ArgumentBinder::Bind`)

**Fix verified (PoC `poc/vm/method-receiver-plain-call.orb` — prints `ALL
TESTS PASSED`, 9 of 9; `ortest` 30/30 and the PoC gate 54/58, the failures
being open findings; `phi-regalloc.orb` green).** The receiver check runs in
every call mode, not only for selector calls. `NormalizeMethod` became
`CheckReceiver`, which finds `self` wherever the call carries it: bound by
currying, pushed by a selector, passed as the first argument, or the first
element of a rest list. A call that carries none of them (`add()`,
`add(x=1)`) binds only other arguments, and `self` is checked when a later
call brings it. `Bind` calls it before the partial-application branch:

```cpp
if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD) && !this->func->shared->IsMethod())
    this->stack_args -= 1;
else if (!this->CheckReceiver()) {
```

So a `self` is checked when a partial application binds it, and a currying
that holds one can be trusted afterwards. A promoted method (a function a
decorator turned into a method of a class) is still exempt: the method it
wraps checks its own receiver, which this fix makes true for every call.
Native methods are checked here too now when called without a selector;
`pcheck.cpp` checks them again, with the same message.

The error now names the class the method expects ("method call requires 'A'
instance as receiver"), as `pcheck.cpp` does; it used to name the type of the
rejected receiver, which read backwards.

The PoC gained the three other ways in (a rest list, a pipe, a partial
application). With the check restricted to selector calls again, four of them
fail and the number crashes. The VM-011 PoC gives the same output before and
after the change.

<details><summary>Original report</summary>

A method is an ordinary function whose first parameter is `self`, and it can
be taken out of its class and called without a selector:

```orbit
get := A.get
get(new A())     // 4
```

`Bind` checks the receiver against the method's class only for a call in
`CallMode::METHOD`, the mode of a selector call:

```cpp
if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD) && !this->NormalizeMethod()) {
```

A call without a selector is not in that mode, so its first positional
argument becomes `self` with no check. The method's code reads `self` with its
own class's slot layout (the inline `self.x` path), so:

- an instance of an unrelated class is read at the wrong offsets:
  `get(new Other())` returns `Other`'s first slot as if it were `A.v`, with no
  error;
- a number as receiver crashes the process: `get(5)` is a SIGSEGV.

The first argument is not the only way in. A rest list (`add([o, 3]...)`) and
a pipe (`o |> add(5)`) deliver the receiver unchecked too, and so does a
partial application: `p := add(new Other())` has too few arguments, so `Bind`
returns a curried function holding the unchecked `self`. From then on that
`self` counts as bound: a selector call checks the bound value only against
the class of the method it calls through, and a decorator that returns such a
function has it installed as it is. *(Added 2026-10-22.)*

Native methods are not affected: `pcheck.cpp` checks `argv[0]` against the
owner type of every native method, whatever the call mode.

**Found** while weighing how a function promoted to a method by a decorator
should check its receiver: whether it can skip the check depends on the
methods it calls checking theirs, and a plain call of a method does not.

**PoC:** [`poc/vm/method-receiver-plain-call.orb`](poc/vm/method-receiver-plain-call.orb)
*(confirmed live: the four controls pass — right receiver, subclass, call
through an instance, a curried method with `self` bound — the unrelated
instance is accepted, and the number crashes)*.

**Fix (suggested, not tried):** check the receiver of a method in `Bind`
whatever the call mode, when `self` is not already bound by currying: in
`CallMode::METHOD` it is the receiver slot `NormalizeMethod` already looks at,
otherwise it is the first positional argument, or the first element of the
rest list when there are no positional arguments. The check has to come
before the partial-application branch of `Bind`, so that a `self` is checked
when it is bound; a curried method whose currying is not empty then had its
`self` checked by the call that bound it.

</details>

---

## VM-011 — A method called through its type receives the type as its receiver
**Severity:** High (a normal call form is refused; a type object passes as `self` and is read with an instance layout) · **Status:** FIXED (2026-10-22) · **Location:** `orbit/orbiter/orbcall.cpp:228` (`ArgumentBinder::Bind`), `orbit/orbiter/orbcall.cpp:138` (`ArgumentBinder::CheckReceiver`)

**Fix verified (PoC `poc/vm/method-through-type.orb` — prints `ALL TESTS
PASSED`, 12 of 12; `ortest` 30/30 and the PoC gate 55/58, the failures being
open findings; `phi-regalloc.orb` green).** Both halves, in `orbcall.cpp`
only; `PUSHIF` and the compiler are unchanged.

1. In the `CallMode::METHOD` block of `Bind`, a type used as selector is not
   a receiver: when the pushed slot holds a type object and the callee is a
   method of anything but a metatype, `DropReceiver` removes it (the explicit
   arguments move down by one, SP and `stack_args` decrease) before `base_sp`
   is fixed, and the first explicit argument is checked as in any other call:

   ```cpp
   if (!this->func->shared->IsMethod())
       this->stack_args -= 1;
   else if (IsTypeObject(*this->ArgsBase()) && !IsMetatype(fiber->isolate, this->func->shared->owner_type))
       this->DropReceiver();
   ```

   The test is on the selector, not on the method's class: `Handler.handle(h)`
   (a method promoted from `Server`) and `Handler.handle2(h)` (a curried
   method of `Server`) drop `Handler` too, and `h` arrives as `me`. A check
   against the method's class would have left `Handler` on the stack as `me`.
2. `CheckReceiver` no longer checks a type receiver through `O_GET_TYPE` (its
   superclass): a type object is `self` only for a method of a metatype
   (`Type`, or `Class` for classes; none has methods today).

`PUSHIF` stayed at first: it saved the push, and with it a shift of every
argument, for the common selector call of a function that is not a method
(`io.print(...)`, a callback in a property). *(2026-10-24: removed after all.
A selector call now pushes its base with a plain `PUSH`, and `Bind` removes
the slot with `DropReceiver` for a callee that is not a method too, so the
decision lives in one place.)*

The PoC passes in full: calls through a class, a subclass and a builtin type;
`D.get(new P())` runs (checked against `P`); `P.get()` is a partial
application; `D.who()` no longer returns the class; `get(D)` is refused.
`ortest/decorators/04_decorator_objects.orb` gained the two decorated methods
called through their class. Mutation-tested: without `DropReceiver` seven
checks fail again, without the type refusal in `CheckReceiver` `get(D)` is
accepted again. `poc/vm/trap-bind-error-argpos.orb` still passes.

Found on the way, filed apart: OBJ-004 (a trait implemented by a superclass
is not seen by `IsTypeExtends`, so its methods refuse instances of a
subclass).

<details><summary>Original report</summary>

`Type.method(obj, args...)` should run `method` with `obj` as its receiver,
as the same method taken into a variable does (`m := Type.method; m(obj)`,
which works). Instead it is refused, for classes and builtin types alike:

```
P.get(p)            -> TypeError: method call requires 'P' instance as receiver
List.append(l, 3)   -> TypeError: method call requires 'List' instance as receiver
```

A selector call emits `PUSHIF`, which pushes the selector's base as the
receiver whenever the callee is a method. Here the base is the type object, so
the type is pushed and `obj` becomes the first ordinary argument;
`CheckReceiver` then checks the type object against `owner_type` and refuses
it. The explicit receiver never takes the receiver's place.

The user confirmed (2026-10-16) that this is a defect: calling a method
through its type is meant to work, with the first argument as the receiver.

**Second half (added 2026-10-22): a type object passes as `self`.**
`CheckReceiver` treats a receiver that is not an instance as a type and checks
it through `O_GET_TYPE`, which for a `TypeInfo` is its superclass. So a
subclass is accepted as `self` for every method of its superclass:

```orbit
class P { pub var v = 5; pub func get() { return self.v }; pub func who() { return self } }
class D : P { }

D.who()        // ok<D>: self is the class D
D.get()        // reads self.v out of the type object with P's layout, no error
get := P.get
get(D)         // the same, without a selector
```

No method takes a type as `self` on purpose: `Class` and `Type` have no
methods. Instrumenting the check over every `ortest` suite and PoC showed type
receivers in two files only, both expecting the refusal (this PoC and
`poc/vm/trap-bind-error-argpos.orb`, which uses `K.get(42)` to raise a binding
error).

**Decided (2026-10-22):**

- `P.get()` with no receiver left is a partial application, as
  `get := P.get; get()` is, not an error;
- the receiver is checked against the method's class, not against the type
  used as selector: `D.get(new P())` runs.

**Found** while discussing decorated methods (IR-011), where calling a
decorated method with an explicit receiver is one of the shapes to support.

**PoC:** [`poc/vm/method-through-type.orb`](poc/vm/method-through-type.orb)
*(confirmed live: both controls pass, the five calls through a type are
refused; a receiver of the wrong type is refused, as it must stay. Since
2026-10-22 also: `D.get(new P())` is refused, `P.get()` is refused instead of
being a partial application, and `D.who()` and `get(D)` accept the class as
`self`)*.

**Fix (suggested, not tried):** two parts.

1. In `Bind`, not in `PUSHIF`. A selector call always counts a receiver slot
   (`arguments = n + 1`); `PUSHIF` pushes it only for a method, and `Bind`
   infers it was pushed from the same `IsMethod()`. If `PUSHIF` skipped the
   push for a type base, `Bind` could not tell and would read a caller's slot
   as `self`. So `PUSHIF` stays as it is, and in the `CallMode::METHOD` block
   of `Bind`: when the pushed receiver is a type object and the method belongs
   to it or to a supertype (`IsTypeExtends(base, owner)`), drop that slot —
   move the explicit arguments down by one, then decrease SP and
   `stack_args` — before `base_sp` is fixed. `CheckReceiver` then sees the
   first explicit argument, as in any other call. This covers classes,
   subclasses, traits (`T.m(obj)`) and natives (`List.append(l, 3)`).
2. In `CheckReceiver`, drop the `O_GET_TYPE` branch: a type object is not a
   valid receiver, except for a method of `Class` or `Type` (none exists
   today; the rule covers them if they come).

`poc/vm/trap-bind-error-argpos.orb` keeps working: `K.get(42)` then has `42`
as its receiver, which is refused.

</details>

---

## VM-010 — A curried call drops its named arguments and `*kwargs`
**Severity:** Medium (silent wrong result: the final call runs with the defaults) · **Status:** OPEN · **Location:** `orbit/orbiter/orbcall.cpp:229` (`ArgumentBinder::Bind`, the currying path)

When a call has fewer positional arguments than the function's arity, `Bind`
returns a curried function instead of calling it. The curried function is
built from the positional arguments alone:

```cpp
this->fiber->vm.regs.RR.reg = (PtrSize) FunctionNew(this->func, this->ArgsBase(), this->stack_args).get();
```

The named arguments (`r10`, `nargs`) and the keyword dict (`r12`, `kwargs`) of
that call are not saved anywhere, and nothing reports it. When the curried
function is completed, the function sees its defaults again:

| Call | Result | Expected |
|---|---|---|
| `g(1, c=9)(2)` on `func g(a, b, c=5)` | 125 | 129 |
| `g(c=9)(1, 2)` | 125 | 129 |
| `kw(1, *{"x": 1})(2)` on `func kw(a, b, *rest)` | `rest` is `{}` | `{"x": 1}` |

The same full calls, with every positional argument given, are right.

**Why it matters now.** Decorators lean on currying for the case of a
decorator configured once and applied many times: with the decorated function
as the first parameter (`func times(f, n=2)`), `by7 := times(n=7)` should be a
decorator waiting for the function, and `@[by7]` should apply `times(f, n=7)`.
Today `@[by7]` applies `times(f)` with the default `n=2`.

Related, to be decided along with the fix: only a parameter with a default can
be named (by design, see `ortest/calls/01_argument_passing.orb`), but naming a
**required** one is not refused. `g(1, b=2)` on `func g(a, b)` takes the same
currying path, drops `b=2`, and returns a curried function where an error
(or a bound `b`) would be expected. A name the function does not have at all
is refused (`'g' does not accept named arguments`).

**Found** while discussing pre-configured decorators with the user.

**PoC:** [`poc/vm/curry-named-args.orb`](poc/vm/curry-named-args.orb)
*(confirmed live: the three controls pass, the five curried checks fail)*.

**Fix (suggested, not tried):** keep the call's `nargs` and `kwargs` in the
curried function next to the positional arguments, and merge them into the
completing call (the completing call's own names win, or a name given twice is
refused: to be decided). The required-parameter case needs its own rule:
either refuse the name, or let a named required parameter count towards the
arity.

---

## VM-009 — An exception context dangles once the VM stack is reallocated
**Severity:** High (use after free; crash, or silent reads of freed memory) · **Status:** FIXED (2026-10-16) · **Location:** `orbit/orbiter/vm.cpp:1962` (`TARGET_OP(TBGIN)`), `orbit/orbiter/vmstack.cpp:36` (`VMStack::Grow`), `orbit/orbiter/excstack.h` (`ExPtrContainer`)

**Fix verified (regression PoC `poc/vm/exception-context-stack-move.orb` —
prints `ALL TESTS PASSED`, 8 of 8 — plus two new PoCs,
`poc/vm/exception-context-module-slot.orb` and
`poc/vm/panic-without-handler.orb`; `ortest` 22/22; PoC gate 45/46, the open
IR-010 being the only failure; `phi-regalloc.orb` green).** `CP` and the link
to the previous context are no longer pointers but offsets from the stack base,
like `SP` and `BP`, and become pointers only where a context is read or
written:

```cpp
// TBGIN
const auto ctx = new(location)ExceptionContext(regs->CP.reg, regs->BP.reg, catch_off);
regs->CP.reg = ctx->GetOffset(fiber->vm.stack.stack);   // offset + 1

inline ExceptionContext *ExContextGet(const void *base, const U32 offset) {
    if (offset == 0)
        return nullptr;
    return (ExceptionContext *) ((unsigned char *) base + (offset - 1));
}
```

Three details of the encoding are load-bearing, and each has a check that
fails without it (mutation-tested by swapping each one back in):

- **The offset is biased by one.** `CP == 0` still means "no context", and a
  context can really sit at offset 0: the module frame has `BP == 0` and its
  first reserved slot is slot 0. Without the bias the first module-level `try`
  reads as "no context" and `TEND` dereferences null: 6 of 11 `vm` PoCs and 8
  of 22 `ortest` suites crash, `exception-context-module-slot.orb` among them.
- **"No context" maps to `nullptr`, not to the stack base.** An intermediate
  version without the bias also lacked the zero check, so `CP == 0` became the
  base of the stack and every `!= nullptr` test passed: a panic with no handler
  read the module's own slots as a context and jumped to a garbage catch
  offset (`Assertion failed: (false)` in the dispatch loop). Only
  `panic-without-handler.orb` catches it; `ortest` stays 22/22.
- **`prev` is shifted, and `SetAction` overwrites.** The previous CP is stored
  as `offset_prev << 2` so the pending action keeps the two low bits, and
  `prev()` shifts it back. `SetAction` must clear the old action before writing
  the new one: an intermediate version OR-ed them, so a pending `return` (3)
  replaced by a `break` (1) still read as `return`. Caught by `ortest`
  `exceptions/01_abrupt_exit.orb` and by the new last check of the stack-move
  PoC, which raises the `break` after the stack grew.

The stack-move PoC gained three checks: two contexts in one frame, each above
a growth (the inner one is released first and CP must point back at the outer
one); a `return` pending in a `finally` across a growth; and a pending `return`
replaced by a `break` raised after a growth.

**Audit of other absolute stack addresses**, as suggested in the original
report. The frame prologue saves `SP` and `BP` as offsets and `IP` as a code
address, so it is unaffected. A generator that yields inside a `try` and is
resumed from 50 frames deeper still catches (probe, same result before and
after the fix). The other `stack->stack + ...` expressions build a pointer for
immediate use, with one case left unverified: `CallNative`, `CallFunction`
and `EvalAsync` hand a native function an `args` pointer into the stack, which
would dangle if the native called back into Orbit on the same fiber and the
callback grew the stack before the native read its arguments again. Not
reproduced, and not filed until it is.

<details><summary>Original report</summary>


`TBGIN` builds a `try` or `trap` context inside the stack buffer and keeps its
**absolute** address:

```cpp
const auto location = stack->stack + (regs->BP.reg + slot);
const auto ctx = new(location)ExceptionContext((ExceptionContext *) regs->CP.reg, ...);
regs->CP.reg = (PtrSize) ctx;
```

That address lives on in `CP` and in the `prev` link of every context opened
after it. Growing the stack is a `realloc`, which may move the buffer, and it
does: under lldb the first growth, from 16 KB to 21 KB, moved it from
`0x7548c0c010` to `0x7549060010`. `SP` and `BP` are offsets into the buffer and
follow it; `CP` and `prev` are pointers and keep pointing into the freed block.

- **A context in every frame crashes.** A recursion that opens a `trap` in each
  frame dies with `SIGSEGV` in `TEND` (`ExPtrContainer::action` on a null
  context) at a depth between 500 and 1,000, the first growth of the stack.
- **A single context above a deep recursion only seems to work.** `trap deep(0)`
  over 50,000 frames returns the right value, but at its `TEND` under lldb
  `CP` was 8 MB below the live buffer: it reads the freed block, which happens
  to be intact.

Not a regression from the VM-007 fix: the crash reproduces identically with the
old `Grow`, since the stack has always grown by `realloc`.

**Found** while extending the VM-004 PoC to its last uncovered refusal, a stack
exhausted inside `Bind`: reaching it in the frame of a `trap` needs a `trap` in
every frame of a deep recursion.

**PoC:** [`poc/vm/exception-context-stack-move.orb`](poc/vm/exception-context-stack-move.orb)
*(confirmed live: SIGSEGV at the third check; the same file at a depth of 300,
where the stack never grows, prints `ALL TESTS PASSED`)*. It is expected to fail
until this is fixed, per GUIDE §11.

**Fix (suggested, not tried):** keep `CP` and the `prev` links as offsets from
the stack base, as `SP` and `BP` already are, and turn them into pointers only
where a context is read or written. Rebasing every live context inside `Grow`
would also work, but it has to find all of them, saved frames included, and
anything else that stores an absolute stack address would still be wrong.
Worth auditing at the same time for other absolute pointers into the stack
held across a call (the frame prologue saves `Context | SP | BP | IP`).

</details>

---

## VM-008 — `PUSHIF` reads a type object as a Function
**Severity:** High (undefined read; confirmed live as a corrupted call) · **Status:** FIXED (2026-10-15) · **Location:** `orbit/orbiter/vm.cpp:1417` (`TARGET_OP(PUSHIF)`)

**Fix verified (new regression PoC `poc/vm/pushif-type-target.orb` — prints
`ALL TESTS PASSED`, 11 of 11; `ortest` 22/22; PoC gate unchanged apart from
the two open findings; `phi-regalloc.orb` green).** `PUSHIF` keeps what
`ResolveCallable` returns and asks it, not the original target:

```cpp
auto target = (Function *) ACCESS_REG_SRC(instr);
...
target = ResolveCallable(fiber->isolate, target);
if (target == nullptr)
    goto ERROR;

if (!target->shared->IsMethod()) {
    DISPATCH;
}
```

The other half of the original question, whether a constructor reached
through a selector binds its receiver the way `Bind` expects, holds: the five
builtin types with a constructor (`Bytes`, `Set`, `Error`, `Chan`, `Context`)
all build it with `RUNTIME_FUNCTION`, never as a method, and an Orbit class has
no constructor of its own to be called through (`K(1)` is refused). So `PUSHIF`
pushes no receiver, `NormalizeMethod` drops the slot reserved for one, and the
two agree.

The PoC calls each of those five types through a selector and compares the
result with a direct call, with keyword arguments, several arguments and none;
it also checks a refused constructor call in argument position and a selector
to something that is not callable. **The defect is now confirmed live, not
only by inspection**: run against the old `PUSHIF`, the bytes near the name
`Error` read as a method flag, the receiver was pushed, and the refused call
`seen_mid("A", trap err.t(), "C")` handed the callee the `Holder` object in
place of `"A"`. The other checks happened to pass there, which is why the first
probe (`Bytes`) found nothing.

<details><summary>Original report</summary>

A method-style call (`obj.attr(...)`) first runs `PUSHIF METHOD`, which pushes
the receiver only when the target is a method:

```cpp
if (ResolveCallable(fiber->isolate, (Function *) target) == nullptr)
    goto ERROR;

if (!((Function *) target)->shared->IsMethod()) {
    DISPATCH;
}
```

`ResolveCallable` accepts two kinds of target: a Function, and a type object
with a constructor, which it resolves to that constructor. The test that
follows ignores the resolution and casts the original target. For a type object
`((Function *) target)->shared` is `TypeInfo::name`, a `const char *`, so
`IsMethod()` reads the `kind` flags of a `FuncShared` that is really the bytes
around the type's name, and pushes the receiver or not according to them.

**PoC:** none. The one shape checked, a builtin type reached through a selector
(`h.t(len=4)` with `h.t = Bytes`), happens to read a clear bit and behaves; the
outcome depends on the bytes near each name, so a reproducer would test the
memory layout, not the defect. Found *by inspection* while verifying VM-004.

**Fix (suggested, not tried):** test the resolved callable,
`auto *callee = ResolveCallable(...); ... if (!callee->shared->IsMethod())`.
Then check that a constructor reached through a selector binds its receiver the
way `Bind`'s `NormalizeMethod` expects.

</details>
---

## VM-007 — Past about 510,000 frames, every call reallocates the whole stack
**Severity:** High (a bottomless recursion hangs for minutes instead of raising a stack overflow) · **Status:** FIXED (2026-10-15) · **Location:** `orbit/orbiter/vmstack.cpp:36` (`VMStack::Grow`)

**Fix verified (regression PoC `poc/vm/stack-growth-near-limit.orb` — prints
`ALL TESTS PASSED` in 1.2 s; `ortest` 22/22; PoC gate unchanged apart from the
open IR-010; `phi-regalloc.orb` green).** When the 30% step would cross the
limit, the stack now takes everything that is left in one step, and refuses
only when even that is not enough for the request:

```cpp
if (this->capacity + increment >= this->limit) {
    increment = this->limit - this->capacity - 1;
    if (increment < size)
        return false;
}
```

`limit - capacity - 1` cannot wrap: the capacity reaches `limit - 1` at most,
where the difference is 0 and the request is refused. Measured across the old
cliff, every depth now answers in about 0.2 s, and the deepest recursion that
fits is the same as before (520,000 frames of a one-argument function
complete, 525,000 raise `OOMError: stack overflow`), so the whole 32 MB is
still usable. Refusing as soon as the 30% step would cross the limit, which was
also considered, would have stopped the stack at the last 30% step instead:
close to the limit with today's numbers, but as low as `limit / 1.3` in
general.

[VM-009](#vm-009--an-exception-context-dangles-once-the-vm-stack-is-reallocated)
was found while checking this fix and is unrelated to it: it reproduces with and
without the change.

<details><summary>Original report</summary>

```cpp
auto increment = std::max(size, (this->capacity * kStackGrowthFactor) / kStackGrowthScalingFactor);

if (this->capacity + increment >= this->limit) {
    increment = size;
    ...
}
```

The stack grows by 30% a step until that step would cross `kMaxStackSize`
(32 MB). From then on each growth is exactly the bytes asked for, so every new
frame in the last stretch before the limit reallocates, and may copy, a stack
of about 31 MB. A recursion through that stretch is quadratic:

| depth (one-argument function) | time |
|---|---|
| 400,000 | 0.19 s |
| 510,000 | 0.19 s |
| 520,000 | > 15 s (CPU-bound) |
| bottomless | minutes before the stack overflow error |

The limit itself is right, and so is the error at the end of it; only the way
there is wrong. The hang in the original VM-004 report was this stretch,
reached by leaked argument slots instead of by frames.

**PoC:** [`poc/vm/stack-growth-near-limit.orb`](poc/vm/stack-growth-near-limit.orb)
*(confirmed live: times out after 30 s)*. It is expected to fail until this is
fixed, per GUIDE §11.

**Fix (verified by probe, not applied):** when the 30% step would cross the
limit, grow to just below the limit in one step:
`increment = std::max(size, this->limit - this->capacity - 1)`. With that
change the PoC prints `ALL TESTS PASSED` in 0.6 s, a bottomless recursion
raises `OOMError: stack overflow` in 0.2 s, and the deepest recursion that fits
is unchanged (520,000 frames complete, 530,000 overflow); the change was then
reverted.

</details>
---

## VM-006 — `!x` disagrees with `if x`: integers always negate to true, falsy objects to false
**Severity:** High (silent wrong boolean, and wrong branch in `if !x`) · **Status:** FIXED (2026-10-14) · **Location:** `orbit/orbiter/vm.cpp:980` (`TARGET_OP(NOT)`)

**Fix verified (regression PoC `poc/vm/not-truthiness.orb` — prints `ALL TESTS
PASSED`, 13 of 13; `ortest` 22/22; `phi-regalloc.orb` green).** `NOT` now asks
the same question as the conditional jumps and the ternary:

```cpp
ACCESS_REG_DST(instr) = BOOL_TO_OBOOL(!IsTrue(value));
```

So `!3` is `false` and `if !n` with `n = 3` skips its branch, while `!""`,
`![]`, `!{}`, `!b""` and `!` of an error Result are `true`.

The rest of the path was checked as the original report asked:

- the compiler emits `NOT` in one other place, `!=` and `!==`, lowered to
  `NOT(EQ)` (`irbuilder.cpp:762`). `EQ` yields a boolean, so the result is
  unchanged; checked live, and `ortest/typehooks/tostr_hooks.orb` (including
  `neq hook` and `eq non-bool traps`) stays green;
- `optimizer.cpp` folds nothing involving `!`;
- every `!` in the stdlib (`repl.orb`, `io/io.orb`, `io/bufio`, `io/file.orb`,
  `regex/pattern.orb`) applies to a boolean (`is`, comparisons, `closed`,
  `failed`, `matched()`), on which the old and the new opcode agree, so no
  stdlib code relied on the defect.

`ortest/net/02` and `04` are back to `check_true(name, !!r)` for a Result;
while this was open they had to spell it `r ? true : false`.

<details><summary>Original report</summary>

The `NOT` opcode decides truth on its own instead of asking `IsTrue`, which is
what the conditional jumps (`JF`, `JT`) and the ternary use:

```cpp
ACCESS_REG_DST(instr) = ((MSize) value == kOddBallTRUE || O_IS_OBJECT(value))
                            ? kOddBallFALSE
                            : kOddBallTRUE;
```

Only `true` and heap objects count as true here. `IsTrue` (`oops.cpp:467`)
treats an SMI as true when it is not zero and lets a heap object answer through
its type's `to_bool`. The two disagree in both directions:

```orb
n := 3
if !n { ... }          // taken: !3 is true
!1                     // true
!""  ![]  !{}  !b""    // false, though each is false to a conditional
!(trap boom())         // false, though an error Result is false to a conditional
```

Every other case checked agrees: `nil`, `false`, `true`, `0`, non-empty
containers, an ok Result.

**Found** while writing the IPV6ONLY checks of `ortest/net/`: a check written
as `check_true(name, !!r)` on an error Result passed for the wrong reason. The
suites use `r ? true : false` instead.

**PoC:** [`poc/vm/not-truthiness.orb`](poc/vm/not-truthiness.orb) *(confirmed
live: 6 of 13 checks fail)*. Each check compares `!x` with
`x ? false : true`. It is expected to fail until this is fixed, per GUIDE §11.

**Fix (suggested, not tried):** `ACCESS_REG_DST(instr) = BOOL_TO_OBOOL(!IsTrue(value))`.
Then check whether the compiler folds or rewrites `!` anywhere else (constant
folding in `optimizer.cpp`, `if !x` lowered to a jump), and audit the stdlib
for code written around the current behaviour.

</details>
---

## VM-005 — Integer literals of 2^32 and more load as their value shifted right by 32
**Severity:** High (silent wrong value for a whole range of integer literals) · **Status:** FIXED (2026-10-14) · **Location:** `orbit/orbiter/vm.cpp:1267` (`TARGET_OP(LDIMM)`)

**Fix verified (regression PoC `poc/vm/ldimm-wide-literal.orb` — prints `ALL
TESTS PASSED`, 7 of 7; `ortest` 22/22; `phi-regalloc.orb` green).** The chunk
is widened to `MSize` as it is fetched, so the shift that places it is done in
64 bits:

```cpp
const auto imm = (MSize) FETCH_IMM(instr);
...
REG_N(dst) = shift == 0
                 ? O_TO_SMI(imm)
                 : O_TO_SMI(O_FROM_SMI(REG_N(dst)) | (imm << (16 * shift)));
```

The PoC builds every expected value at run time, so it compares the two paths
rather than two copies of the same literal. Checked by hand as well, against
the edges of the range `LDIMM` serves: `2^32`, `2^48`, the last SMI (`2^61 - 1`,
four chunks), the first value past it (`2^61`, which leaves `LDIMM` for the
constant pool), their negatives and the hexadecimal spellings; all load as
written. The cause had already been confirmed by probing this same change before
the fix landed, and the unfixed decoder fails 6 of the 7 PoC checks.

`ortest/net/02` and `04` pass the 2^32 + 1 bind flag as the literal
`0x100000001` again; while this was open they had to compute it at run time.

<details><summary>Original report</summary>

An integer literal small enough to be an SMI but at least 2^32 loads as the
wrong number, with no diagnostic:

```orb
io.print(4294967295)        // 4294967295   correct
io.print(4294967296)        // 1            expected 4294967296
io.print(1099511627776)     // 256          expected 2^40
io.print(281479271677953)   // 65537        expected 2^48 + 2^32 + 1
io.print(0x100000001)       // 1
io.print(-4294967297)       // -1
```

The same values computed at run time (`65536 * 65536`) are correct, and so are
literals beyond the SMI range (`0x7FFFFFFFFFFFFFFF`), which go through the
constant pool. Only `LDIMM` is involved.

`Builder::LoadImmediate` splits a literal wider than 16 bits into 16-bit
chunks, each an `LDIMM` carrying its chunk and a shift count, and the VM ORs
them together:

```cpp
const auto imm = FETCH_IMM(instr);           // (instr) & 0xFFFFu: a 32-bit unsigned int
...
REG_N(dst) = shift == 0 ? O_TO_SMI(imm) : O_TO_SMI(O_FROM_SMI(REG_N(dst)) | (imm << (16 * shift)));
```

`imm << (16 * shift)` is computed in 32 bits. For the chunk of bits 32-47 the
shift is 32, which is undefined behaviour in C++; for bits 48-63 it is 48. On
ARM64 the hardware takes the count modulo 32, so the third chunk lands on bits
0-15 and the fourth on bits 16-31, which is every observed value above. Other
targets may produce something else again. The encoding and the compiler are
right; the decoder is not.

**Found** while testing the bind flags of `::orbit::net`: a check meant to pass
`0x100000001` (2^32 + 1) received `1`, which is `REUSEADDR`, and the bind
succeeded. `ortest/net/02` and `04` build that value at run time until this is
fixed.

**PoC:** [`poc/vm/ldimm-wide-literal.orb`](poc/vm/ldimm-wide-literal.orb)
*(confirmed live: 6 of 7 checks fail)*. It is expected to fail until this is
fixed, per GUIDE §11.

**Fix (verified by probe, not applied):** widen before shifting,
`((MSize) imm << (16 * shift))`. With that one change the PoC prints `ALL TESTS
PASSED`; the change was then reverted, so the finding stays OPEN.

</details>
---

## VM-004 — A call that fails while binding its arguments leaves them on the stack
**Severity:** High (silent wrong arguments in the enclosing call; unbounded stack growth that ends in a hang) · **Status:** FIXED (2026-10-14) · **Location:** `orbit/orbiter/orbcall.cpp:166` (`ArgumentBinder::Bind`)

**Fix verified (regression PoC `poc/vm/trap-bind-error-argpos.orb` — prints
`ALL TESTS PASSED`, 20 of 20 in 1.1 s; `ortest` 22/22; PoC gate 38/38;
`phi-regalloc.orb` green).** `Bind` now leaves the stack as it found it on every
way out that refuses the call:

- the shared failure path ends at an `ERROR:` label that restores
  `SP = base_sp` (`orbcall.cpp:334`). Every refusal after `base_sp` is fixed goes
  through it: stack exhausted, constructor without its arguments, too many
  arguments (both checks, with and without a spread or curried arguments),
  allocation of the synthesized rest list, named arguments without defaults,
  keyword arguments refused;
- the two refusals before `base_sp` exists pop what the caller pushed
  themselves: a callee that is not callable (`p_count` slots, `orbcall.cpp:175`)
  and a method given a receiver of the wrong type (`stack_args` slots).

The count of the first is right because of how the compiler emits a method
call: the receiver is pushed by `PUSHIF METHOD` only when the target is a
method, and `PUSHIF` itself refuses a target that is not callable before any
argument of the call is pushed. So `Bind` meets a callee that is not callable
only in a call that is not a method call, where `p_count` is exactly what was
pushed. `SPWN`, `DEFER` and `EvalSync` (`runtime.cpp`) bind through the same
function and only `goto ERROR` afterwards, so they get the restore without
popping twice.

The PoC was rewritten to cover every refusal reachable from Orbit code, not
only the one first reported:

- **in argument position**, fourteen shapes: too many arguments (last and
  middle position, many), not callable, wrong receiver, constructor without
  its arguments, too many on a curried function, a spread longer than the gap
  and one past the arity, a named argument without defaults, keyword arguments
  refused (alone, and after defaults and a rest list were pushed), a native
  callee, and a `spawn`; plus two controls;
- **in a loop**, in the frame of the `trap` (a frame that returns resets SP
  anyway and would hide the leak), 150,000 refusals of 32 arguments for each of
  the three ways out (the `ERROR` label, not callable, wrong receiver) and for a
  `defer` caught by a `try` in the same frame. A leak that size is past the
  32 MB stack several times over.

Run against the binder as it was before the fix, all fourteen shapes fail and
the loops hang; with the fix, everything passes. The one refusal not covered is
a stack exhausted inside `Bind` (`EnsureStack`): reaching it takes a recursion
past the point where [VM-007](#vm-007--past-about-510000-frames-every-call-reallocates-the-whole-stack)
makes every frame reallocate the stack.

The hang in the original report was this leak meeting VM-007: leftovers piled
up until the stack entered the region where it grows one request at a time.

Two defects were found while writing the new PoC and are filed on their own:
[VM-008](#vm-008--pushif-reads-a-type-object-as-a-function) and
[IR-010](ir.md).

**Note (2026-10-15):** with VM-007 fixed, the uncovered refusal (a stack
exhausted inside `Bind`) is now waiting on
[VM-009](#vm-009--an-exception-context-dangles-once-the-vm-stack-is-reallocated)
instead: reaching it in the frame of a `trap` needs a `trap` in every frame of
a recursion deep enough to grow the stack.

**Note (2026-10-16):** VM-009 is fixed, so nothing blocks that last refusal
any more; the PoC does not cover it yet.

<details><summary>Original report</summary>

When `ArgumentBinder::Bind` refuses a call, too many positional arguments
being the case reproduced, the arguments already pushed for that call stay on
the stack. Nothing restores SP to `ArgsBaseSP()`. Every other exit path does,
since VM-002, because it leaves through a frame whose prologue records that SP;
a binding failure leaves **before any frame exists**, so that mechanism never
runs. Under `trap` the catch edge resumes in the caller with SP still above the
dead arguments, and two things follow.

**Wrong arguments.** Used as an argument, the trapped call's result is pushed on
top of the leftovers, and the enclosing callee reads its parameters from the
top of the stack:

```orb
func one(a) { return a }
func show(a, b, c) { io.print(a, "|", b, "|", c) }

show("A", "B", trap one(1, 2))      // 1 | 2 | error      expected A | B | error
show("A", trap one(1, 2), "C")      // 2 | error | C      expected A | error | C
```

The corruption reaches further out than the call that holds the `trap`: in the
PoC, the label argument of the `check(...)` wrapping that call is overwritten
too. A native callee fails the same way
(`trap net.parse("1.2.3.4", 1, @INET, 9)`). A failure inside the callee's body
(`trap boom(1)`) and a `trap` in assignment position both read correctly.

**Stack growth.** Assignment position reads correctly but still leaks the slots
on every failure. A loop running `r := trap one(1, 2, 3, 4, 5, 6, 7, 8)` reaches
400,000 iterations in 0.7 s, then **hangs** before 600,000. The same loop with
`trap boom(1)` or `trap one(1)` runs 1,000,000 iterations in under 1 s. That
the process hangs, rather than reporting a stack overflow, may be a defect of
its own and is worth checking once the leak is gone.

**Found** while writing `ortest/net/05_udp_datagrams.orb`:
`io.print("label", trap h.send(b"x", addr))` printed the address where the label
should have been (`to` is keyword-only, so the call fails in the binder).

**PoC:** [`poc/vm/trap-bind-error-argpos.orb`](poc/vm/trap-bind-error-argpos.orb)
*(confirmed live: 4 of 6 checks fail; the two controls pass)*. It is expected to
fail until this is fixed, per GUIDE §11.

**Fix (suggested, not tried):** on `CallResult::ERROR` from `Bind`, restore SP
to `binder.ArgsBaseSP()` before `goto ERROR`, so that a refused call leaves the
stack as it found it, which is what a call that returns already does. Check the
other callers of `Bind` (`SPWN`, `DEFER`, `runtime.cpp:536`) for the same
pattern, and whether the callee slot and any receiver below `ArgsBaseSP()` need
popping too.

## VM-003 — `break` out of a `try` block is silently ignored
**Severity:** High (silent wrong control flow, no diagnostic) · **Status:** FIXED (2026-10-05) · **Location:** `orbit/liftoff/ir/jblock.cpp:94` (`FindLabeledBlock`), `orbit/orbiter/vm.cpp` (`TARGET_OP(TEND)`)

**Fix verified (regression PoC `poc/vm/break-out-of-try.orb` and the new
`ortest/exceptions/01_abrupt_exit.orb`, both green; each fix mutation-tested by
restoring the old line and watching the suite fail).**

The cause was in the compiler, not in the VM, and the original report's reading
of it was wrong: the machinery was not "in place with only the last step
missing", it was never invoked. `FindLabeledBlock` resolved an unlabelled target
to the innermost jump block that was not `SYNC`, which for a jump inside a `try`
is the TCF block. Two consequences followed, and together they account for the
symptom exactly:

- `GetJBlockEnd` accepts a TCF block and returns the **end of the try**, so the
  `break` left the try and the loop carried on to the next iteration;
- the walk in `visitJump` looking for interposed try blocks is
  `for (cursor = j_chain; cursor != b_target; ...)`, and with `b_target` already
  the head of the chain its body never ran. **No `TSPA` was emitted at all**, so
  the pending-action path in `TEND` was dead code for this shape.

The unlabelled case now returns only a `LOOP` or a `FOR_IN`, which is the rule
the labelled branch of the same function already used.

Two further defects surfaced once the jumps started reaching the VM, both fixed
in the same pass:

- **the propagation in `TEND` re-entered its own finally.** When an inner try
  completed abruptly and the outer context already carried an action, the outer
  context's finally is the block being executed, and `JMP_TO(outer->finally_off)`
  restarted it forever. The action pending on the outer context is now read
  before being overwritten, and the jump is taken only when there was none.
- **`ExPtrContainer::SetAction` accumulated instead of replacing.** The actions
  are packed two bits per context, so OR-ing two of them produces a third
  (`BREAK | CONTINUE == RETURN`). Reachable precisely once a finally can complete
  abruptly while another action is pending.

The resulting semantics, pinned down by the suite: an abrupt completion inside a
finally **replaces** the pending one, as in Java. `break` in a finally over a
pending `return` breaks; `return` in a finally over a pending `break` returns.

[IR-008](ir.md) was the `continue` spelling of the same root cause and is closed
with it. [IR-009](ir.md) was found while validating this and is unrelated: the
reproducer that exposed it contains no jump.

<details><summary>Original report</summary>

A `break` whose loop is outside the enclosing `try` does not leave the loop. It
raises nothing and prints nothing: the loop simply runs to completion.

```orb
var n = 0
for var i in [1, 2, 3, 4, 5] {
    try {
        if i == 3 { break }
        n += 1
    } catch e {
        n += 100
    }
}
// n == 4, expected 2
```

The machinery is in place and only the last step misses. `irbuilder` emits a
`TSPA` with `PendingAction::BREAK` and packs the target block's offset into the
operand (`GetJBlockEnd`), `TEND` reads the pending action and does
`JMP_TO(target)` with `target = ctx->ret_pops`. That jump lands somewhere that
resumes the loop instead of leaving it.

**Not a regression** from the `ExceptionContext::SP` work: verified by building
both revisions and running the reproducer above, which prints `n = 4` on each.
Note, however, that the writes to `ret_pops` are now commented out (see the
**Dead field** note under [VM-002](#vm-002--a-deferred-control-transfer-in-argument-position-overwrites-the-enclosing-calls-arguments)),
so whoever fixes this has to restore a way to carry the target offset first: the
value `TEND` reads today is whatever the stack slot last held.

**PoC:** none filed; the ten lines above reproduce it directly *(confirmed
live)*. A PoC belongs here once the fix lands, together with the `continue`
half tracked as [IR-008](ir.md).

**Fix:** carry the `TSPA` operand through to `TEND` again (a field of its own
rather than the overloaded `ret_pops`), and confirm the offset it jumps to is
the loop's exit block. See also IR-008: the `continue` spelling of the same
construct does not even compile.

</details>

</details>
---

## VM-002 — A deferred control transfer in argument position overwrites the enclosing call's arguments
**Severity:** High (silent wrong arguments; writes below the argument region when the inner call has more arguments than the outer has pushed) · **Status:** FIXED (2026-10-01) · **Location:** `orbit/orbiter/fiber.cpp` (`PushState`, `PopStateNoCtxRestore`)

**Fix verified (regression PoC `poc/vm/trap-argpos-clobber.orb` — prints `ALL
TESTS PASSED`; `ortest` 19/19).** Both halves went away at once, and neither was
fixed where it was reported: the cure was to stop deriving the stack pointer on
the way out of a frame and start recording it on the way in.

The call frame prologue gained a fourth word. `PushState` now takes the bottom
of the argument region and saves it alongside BP and IP (`Context | SP | BP |
IP`), and `PopStateNoCtxRestore` reads it back. Returning from a frame therefore
restores SP to exactly where the caller was before it pushed anything, on every
exit path there is, and no path has to carry a count any more:

- the **trapped panic** half: `UnwindStack` walks frames through that same
  function, so the catch edge lands on the right SP without touching it;
- the **`return` through a `finally`** half: the deferred `RET` at
  `TARGET_OP(TEND)` calls `Return`, which calls `PopState`, which now restores
  the recorded SP. The `pops` it is handed is ignored, which is just as well:
  the original code read `ctx->ret_pops` *after* `ReleaseExceptionContext` had
  zeroed the context, so it had always been passing 0. That read-after-clear was
  the true root cause of this half, and it is now moot.

The value saved is `ArgumentBinder::ArgsBaseSP()`, fixed inside `Bind` right
after receiver normalization and before the binder pushes anything itself. It is
deliberately not derived from the bound argument count: the binder pushes slots
it does not count (the rest list, the kwargs dict) and drops the receiver of a
method-mode call whose callee is not a method, because that slot belongs to the
caller. Both discrepancies were found by this work and are what made a
count-based cleanup unfixable in general.

The `pops` parameter of `Return`, the 16-bit POP VALUES field of `RET`, the
RETURN operand of `TSPA` and `visitReturn`'s `pops_slot` are all dead as a
result, and `ExceptionContext::ret_pops` now carries only the BREAK/CONTINUE
jump target (see [VM-003](#vm-003--break-out-of-a-try-block-is-silently-ignored)).

<details><summary>Original report (trapped panics)</summary>

When a `trap` expression is passed **as an argument to a call** and the trapped
expression **panics**, the panicking call's own arguments end up in the slots
the enclosing call has already pushed. The outer callee runs with values it was
never passed.

```orb
func boom2(x, y) { panic Error(@X, "no") }
func show(a, b, c, d) { io.print(a, b, c, d) }

show("A", "B", trap boom2(10, 20), "D")    // a=10  b=20  c=error  d="D"
```

The damage scales with the arity of the trapped call, and is positional rather
than a clean overwrite (`seen4` prints `a|b|d`):

| trapped call | expected | observed |
|---|---|---|
| `trap boom1(11)` | `A\|B\|D` | `B\|11\|D` |
| `trap boom2(10, 20)` | `A\|B\|D` | `10\|20\|D` |
| `trap boom3(1, 2, 3)` | `A\|B\|D` | `2\|3\|D` |

The last row is the reason for the severity: two arguments had been pushed and
three were written over them, so one write landed **below** the enclosing call's
argument region, into whatever the frame keeps there. The PoC survives it, which
is luck, not safety.

Three controls narrow it down, and all three are green:

- the identical shape with a call that **returns normally** (`trap sum2(10, 20)`)
  binds every argument correctly, so this is the catch edge and not the
  argument-push sequence by itself;
- `trap` over a non-call, and a plain nested call in the same position, are both
  correct;
- `trap` in **assignment** position (`r = trap boom2(10, 20)`, the form every
  existing suite uses) is correct, which is why `ortest/calls/01_argument_passing.orb`
  has never caught it: it only ever traps into a variable.

**Hypothesis** (not confirmed against the bytecode): on the catch edge SP is not
restored to the enclosing call's argument cursor, so the inner call's pushes are
laid down from the wrong base. Same family as VM-001, which was the previous
SP-restoration defect on this path; the fix there deliberately stopped touching
SP at all, and an argument cursor belonging to a call that is still being built
is a case that fix does not cover.

**PoC:** [`poc/vm/trap-argpos-clobber.orb`](poc/vm/trap-argpos-clobber.orb)
*(confirmed live — 3 of 8 checks fail; it is expected to fail until this is
fixed, per GUIDE §11)*. Found while writing `ortest/net/`, where every refusal
check had the shape `check_error(name, trap sock.recv(-1), kind, substr)` and
the failing test printed a corrupted label; all 41 sites there were rewritten to
hoist the `trap` into a variable first.

**Fix:** restore SP on the catch edge to the cursor of the call currently being
assembled, not to the frame's base, so the arguments already pushed for it stay
reserved. Then drop the hoisting workaround in `ortest/net/` and let those
suites use `trap` inline again.

</details>

---

## VM-001 — Spill slots clobbered after a trapped panic (SP rewound to end of exception block)
**Severity:** High (silent wrong-value reads after any `trap` that actually catches) · **Status:** FIXED (2026-07-08) · **Location:** `orbit/orbiter/vm.cpp` (`UnwindStack`, catch branch)

**Fix verified (regression PoC `poc/vm/trap-unwind-sp.orb` — prints `ALL TESTS
PASSED`).** Root cause: on catch, `UnwindStack` rewound SP to the **end of the
ExceptionContext block**:

```cpp
regs->SP.reg = ((unsigned char *) (regs->CP.reg + sizeof(ExceptionContext))) - stack->stack;
```

a leftover of the old trap system, which assumed the exception block was the
top of the frame. LinearScan spill slots are allocated in the prologue *above*
the exception block (`GetFreeStackSlot` keeps counting past the
`StackSlotGuard` words), so the first SP-relative `PUSH` after the catch wrote
its argument straight into a BP-relative spill slot; the next `SKLDR` then
reloaded the pushed value (a String/Bytes) instead of the spilled one (e.g. a
global NativeFunc) — hence "invalid call to a non-callable object" / wrong
member lookups on the second call after a caught panic.

The fix is to **not touch SP at all**: the frame-pop loop above has already
left SP at the caller's value at call time (base of the saved `FiberContext`),
i.e. with the entire prologue — exception block and every slot allocated after
it — still reserved. The trap block lives in the frame prologue and stays
reserved for as long as the frame exists. LinearScan needed no change (the
same bytecode ran correctly whenever the panic did not fire).

<details><summary>Original report</summary>

**Reproducer (minimal):**

```orb
import "io"

func boom() { panic Error(@X, "no") }

e := trap boom()

io.print("first")     # ok
io.print("second")    # AttributeError: 'Bytes' object has no property 'print'
```

After a panic is **caught** by `trap`, the *second* global access that follows
reads a stale value: `io` resolves to a leftover `Bytes` (plausibly
`io.print`'s own argument-conversion buffer from the first call). Variants:

```orb
# any callable global after the trap:
func hello() { return "hello" }
e := trap boom()
io.print(hello())   # TypeError: invalid call to a non-callable object('String')
```

- A single global access after the trap is fine; it's from the second one on
  that values are wrong.
- `trap` over a call that does **not** panic is fine (baseline passes).
- The panicking function needs no arguments; any trapped panic triggers it.
- First observed as `regex.Pattern.replace` tests failing *after* an earlier
  `trap p.replace(...)` TypeError test in the same script.
- A spurious extra newline is printed by the first `io.print` after the trap —
  corruption is already present at that point, it just doesn't crash yet.

**Initial hypothesis** (superseded by the confirmed root cause above): the
exception edge was suspected of leaving the register *file* inconsistent with
the allocator's model. In reality the registers and the reloads were fine — it
was the spill slots' *memory* being overwritten by post-catch PUSHes, because
of the SP rewind. Related history: `Fiber::PopState` SP leak (already fixed)
lived on this same unwind path.

</details>
