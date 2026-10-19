# symtable — bug report

**Component:** `orbit/liftoff` (symtable.h, symtable.cpp) · **ID prefix:** `SYM`
**PoCs:** [`poc/symtable/`](poc/symtable/) · **Last reviewed:** 2026-10-19
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## SYM-002 — A variable captured two levels down reaches the innermost function through an unset closure
**Severity:** High (SIGSEGV on a nested closure) · **Status:** OPEN · **Location:** `orbit/liftoff/symtable.cpp:623` (`LookupInsert`)

When a nested function reads a variable of an enclosing function,
`LookupInsert` marks the variable `CLOSURE` and flags the scope that reads it:

```cpp
sym->location = StorageLocation::CLOSURE;
this->scope->flags |= ScopeFlags::CLOSURE;
```

Only that scope. When the variable lives two or more levels up, the functions
in between are not flagged, so none of them loads its closure into its frame
(`LoadClosureObject`, done only for a `CLOSURE` scope). The innermost function
is still built with the `CLOSURE` flag, and `LDFUNC` reads the middle
function's closure slot, which was never set: the innermost function gets no
usable closure and its first read crashes (SIGSEGV).

```orbit
func outer() {
    n := 5
    func mid() {
        func inner() { return n }   // mid never reads n itself
        return inner()
    }
    return mid()
}
```

A captured local, a parameter and `self` all crash the same way. A middle
function that copies the value into a local of its own (`m := n`) works.

Not related to SYM-001, which was about one level, and present before it.
**Found** while checking that functions nested in a method inherit the
method's class (method-decorator plan, step 1): a closure two levels inside a
method crashed for this reason.

**PoC:** [`poc/symtable/sym-002.orb`](poc/symtable/sym-002.orb)
*(confirmed live on HEAD: the control passes, the first two-level capture
crashes)*.

**Partial fix (2026-10-19, staged):** `LookupInsert` now flags every
function scope between the reader and the declaring scope, so each middle
function loads its closure and hands it down:

```cpp
for (auto *cursor = this->scope; cursor != sym->decl_scope; cursor = cursor->back) {
    if (cursor->type == ScopeType::FUNCTION)
        cursor->flags |= ScopeFlags::CLOSURE;
}
```

That removes the crash when the middle functions declare no captured
variables of their own: the first four checks of the PoC pass. It is not
enough when a middle function declares one, and the PoC gained five checks
for that case. Six checks fail in all:

| Check | Result | Expected |
|---|---|---|
| one value from each enclosing level (`n` in `outer`, `m` in `mid`) | 20 | 11 |
| a middle function's parameter, two calls | `(0, 0)` | `(1, 2)` |
| a middle function's local, two calls | `(40, 40)` | `(110, 120)` |
| a middle function that recurses | error | 210 |
| a function declared after the one that captures it | error | 2 |
| a write two levels down reaches the declaring function | 1 | 11 (IR-014) |

The cause is that closures are flat: one object per call of a function that
declares captured variables, with no link to the closure of the function
around it. A function that both declares captured variables and reads an
enclosing one (`mid`) needs two closures and has room for one. Today its
prologue emits `CLONEW` into the frame's closure slot and then, because the
scope is flagged `CLOSURE`, `LDCLO` into the same slot
(`irbuilder.cpp:1099-1114`): the closure it received replaces the one it just
created, and `m := 10` lands in slot 0 of `outer`'s closure, over `n`.

Giving all the nested variables distinct slots in one shared object does not
work either: `m` exists once per call of `mid`, and two closures made by two
calls of `mid` (or a recursion, or two fibers) are alive at the same time,
each needing its own `m`. A table laid out at compile time has room for one.

**Fix (decided 2026-10-19, not yet implemented): linked closures.** Every
closure keeps a link to the closure of the function around it, and a captured
variable is addressed by a pair (depth, index) decided at compile time.

- **`Closure` gets a `parent` field**, and `ClosureTrace`
  (`closure.cpp:14`) visits it, so a closure keeps the enclosing one alive
  after the enclosing function has returned.
- **Only a function that declares captured variables of its own creates a
  closure**, at every call, in its prologue (`CLONEW`, as today), with
  `parent` set to the closure it was created with (`this_func->closure`). Its
  frame's closure slot holds the new closure; the `LDCLO` into the same slot is
  no longer emitted for it.
- **A middle function that declares none creates nothing**: it loads the
  closure it received into its frame (`LDCLO`, as today) and hands it down to
  the functions it creates.
- **Each function numbers only its own captured variables**, from 0
  (`closure_count` per scope, as `ComputeLocalVarOffset` does today): the
  index of a variable is its slot in the closure of the function that declares
  it.
- **The depth is computed by the compiler** for every read and write: walk
  the scopes from the one that reads up to the one that declares, counting the
  scopes that create a closure (`closure_count > 0`), the declaring one
  excluded. A function reading its own captured variable uses depth 0.
- **`CLOLDR` and `CLOSTR` carry the depth in their 4 reserved bits**
  (`opcode.h:192-193`): the VM follows `parent` that many times, then reads
  or writes the slot. That allows 15 levels; deeper nesting should be a
  compile error rather than a silent wrap.

For the PoC's `outer`/`mid`/`inner`: `outer`'s closure is `[n]`; each call
of `mid` creates `[m]` with `parent` = `outer`'s closure; `inner` reads `n`
at (1, 0) and `m` at (0, 0). Two calls of `mid` make two `[m]` closures that
share one `outer` closure, so each `inner` keeps its own `m` and every one of
them reads and writes the same `n`.

The write check also needs IR-014, which is independent: the declaring
function must load the slot again after a call instead of reusing the value
it stored.

---

## SYM-001 — A parameter captured by a nested function gets no closure slot
**Severity:** High (SIGSEGV on a basic closure; every wrapping decorator hits it) · **Status:** FIXED (2026-10-16) · **Location:** `orbit/liftoff/symtable.cpp:109` (`ComputeLocalVarOffset`, the `PARAMETER` branch)

**Fix verified (PoC `poc/symtable/sym-001.orb` — prints `ALL TESTS PASSED`,
7 of 7; `ortest/decorators/01_functions.orb` 13/13; `ortest` 23/24 and the PoC
gate 46/49, the failures being the open IR-010, IR-011 and IR-012;
`phi-regalloc.orb` green).** A captured parameter now gets both numbers, the
way `LoadParameter` expects them: `stack_offset` is its parameter index and
`offset` its closure index.

```cpp
if (symbol->type == SymbolType::PARAMETER) {
    if (symbol->location == StorageLocation::CLOSURE) {
        symbol->stack_offset = scope->parameter_count++;
        symbol->offset = scope->closure_count++;
    } else
        symbol->offset = scope->parameter_count++;

    continue;
}
```

The enclosing function now creates its closure, and the nested function reads
the captured value from it. A closure that reads `self.prop` still crashes,
but that is IR-012, which this fix unblocks.

**Note (2026-10-16):** an intermediate version dropped the `continue` and let
parameters fall through to the storage-location `switch`. It produced correct
values, but only because parameters are the first symbols laid out in their
scope, so `stack_count` happened to equal each parameter's index. It also
counted every parameter as a local: `three(a, b, c)` with no locals got
`vars_count` 3 instead of 0, so every call reserved and zeroed one unused slot
per parameter, and the deepest recursion fell from 524,286 to 466,032 frames
(one argument) and from 419,429 to 322,637 (three). The final version keeps
the `continue`, and both depths are back to their old values.

<details><summary>Original report</summary>


When a nested function reads a parameter of the function around it,
`LookupInsert` marks the parameter `StorageLocation::CLOSURE` and the nested
scope `ScopeFlags::CLOSURE`. The slots are laid out afterwards, and
`ComputeLocalVarOffset` handles parameters before it looks at the storage
location:

```cpp
if (symbol->type == SymbolType::PARAMETER) {
    symbol->offset = scope->parameter_count++;

    continue;
}
...
case StorageLocation::CLOSURE:
    symbol->offset = scope->closure_count++;
    symbol->stack_offset = scope->stack_count++;
```

A captured parameter therefore never counts towards `closure_count`. Then:

- the enclosing function sees `ShouldCreateClosure() == false`, so it emits
  no `CLONEW` and reserves no slot for the closure object;
- `CaptureParametersIntoClosure` still finds the parameter marked
  `CLOSURE` and stores it, at a closure offset that was never assigned;
- the nested function is built by `LDFUNC` with the `CLOSURE` flag, which
  reads the enclosing frame's closure slot, here past the end of the frame
  and zero: the function gets a null closure;
- its first read of the captured value, `CLOLDR`, calls
  `ClosureGet(nullptr, …)`: SIGSEGV (`closure.cpp:43`, lock at address
  `0x18`; confirmed under lldb).

A captured **local** variable takes the `CLOSURE` case and works, as does
copying the parameter into a local first (`n := p`). `self` is a parameter, so
a closure in a method that uses `self` crashes too.

This is what makes decorators look broken on plain functions: any decorator
that returns a wrapper around its argument (`func deco(f) { func w(x) {
return f(x) } return w }`) captures the parameter `f`.

**Found** while building the decorator suites (`ortest/decorators/`) for
IR-011. No suite exercised a captured parameter before.

**PoC:** [`poc/symtable/sym-001.orb`](poc/symtable/sym-001.orb)
*(confirmed live: crashes at the first captured parameter; the captured local
passes)*.

**Fix (suggested, verified by probe):** give a captured parameter both
numbers, the way `LoadParameter` already expects (`stack_offset` as the
parameter index, `offset` as the closure index):

```cpp
if (symbol->type == SymbolType::PARAMETER) {
    if (symbol->location == StorageLocation::CLOSURE) {
        symbol->stack_offset = scope->parameter_count++;
        symbol->offset = scope->closure_count++;
    } else
        symbol->offset = scope->parameter_count++;

    continue;
}
```

With this change the PoC passes 7/7, `ortest/decorators/01_functions.orb`
passes 13/13, and `ortest` and the PoC gate are otherwise unchanged; the probe
was then reverted. It does not fix reading `self.prop` inside the closure,
which is IR-012.

</details>
