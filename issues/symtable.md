# symtable — bug report

**Component:** `orbit/liftoff` (symtable.h, symtable.cpp) · **ID prefix:** `SYM`
**PoCs:** [`poc/symtable/`](poc/symtable/) · **Last reviewed:** 2026-10-16
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

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
