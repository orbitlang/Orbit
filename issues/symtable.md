# symtable — bug report

**Component:** `orbit/liftoff` (symtable.h, symtable.cpp) · **ID prefix:** `SYM`
**PoCs:** [`poc/symtable/`](poc/symtable/) · **Last reviewed:** 2026-10-16
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## SYM-001 — A parameter captured by a nested function gets no closure slot
**Severity:** High (SIGSEGV on a basic closure; every wrapping decorator hits it) · **Status:** OPEN · **Location:** `orbit/liftoff/symtable.cpp:109` (`ComputeLocalVarOffset`, the `PARAMETER` branch)

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
