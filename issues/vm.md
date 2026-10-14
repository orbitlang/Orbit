# vm — bug report

**Component:** `orbit/orbiter` (interpreter: trap/panic unwind, register file) · **ID prefix:** `VM`
**PoCs:** [`poc/vm/`](poc/vm/) · **Last reviewed:** 2026-10-14
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## VM-006 — `!x` disagrees with `if x`: integers always negate to true, falsy objects to false
**Severity:** High (silent wrong boolean, and wrong branch in `if !x`) · **Status:** OPEN · **Location:** `orbit/orbiter/vm.cpp:977` (`TARGET_OP(NOT)`)

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
**Severity:** High (silent wrong arguments in the enclosing call; unbounded stack growth that ends in a hang) · **Status:** OPEN · **Location:** `orbit/orbiter/vm.cpp:1099` (`TARGET_OP(CALL)`, `binder.Bind` → `goto ERROR`)

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
