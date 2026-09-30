# vm — bug report

**Component:** `orbit/orbiter` (interpreter: trap/panic unwind, register file) · **ID prefix:** `VM`
**PoCs:** [`poc/vm/`](poc/vm/) · **Last reviewed:** 2026-09-29
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## VM-002 — A trapped panic in argument position overwrites the enclosing call's arguments
**Severity:** High (silent wrong arguments; writes below the argument region when the inner call has more arguments than the outer has pushed) · **Status:** OPEN · **Location:** `orbit/orbiter/vm.cpp` (`UnwindStack`, catch branch) / `orbit/liftoff/codegen.cpp` (argument push sequence)

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
