# vm — bug report

**Component:** `orbit/orbiter` (interpreter: trap/panic unwind, register file) · **ID prefix:** `VM`
**PoCs:** [`poc/vm/`](poc/vm/) · **Last reviewed:** 2026-07-08
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

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
