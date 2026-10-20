# symtable — bug report

**Component:** `orbit/liftoff` (symtable.h, symtable.cpp) · **ID prefix:** `SYM`
**PoCs:** [`poc/symtable/`](poc/symtable/) · **Last reviewed:** 2026-10-19
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## SYM-003 — A nested function reaches a name the enclosing function declares only later
**Severity:** High (silent wrong value, or a call to `nil`) · **Status:** FIXED (2026-10-19) · **Location:** `orbit/liftoff/symtable.cpp:121` (`ComputeLocalVarOffset`, the `UNKNOWN` branch)

**Fix verified (PoC `poc/symtable/sym-003.orb` — prints `ALL TESTS PASSED`,
6 of 6; `ortest` 24/26 and the PoC gate 49/56, the failures being open
findings; `phi-regalloc.orb` green).** The `UNKNOWN` branch of
`ComputeLocalVarOffset` no longer aliases a name to a declaration of an
enclosing **function**; the name falls through to `GLOBAL`, which finds a
module name at run time or raises `NameError`:

```cpp
auto *tmp = LookupInEnclosing(symbol->name, scope);
if (tmp != nullptr && tmp->decl_scope->type != ScopeType::FUNCTION) {
    symbol->alias = tmp;
    continue;
}
```

A comment above the test explains why: a name that was not declared where
it is used can only match a declaration that comes after the use, and inside
a function a name is visible only after its declaration. Aliases to module
declarations (keys looked up at run time) and to class and trait members are
kept, so module-level forward references still work.

The PoC checks that a sibling function and a local declared later, and a
function declared later used two levels down, now raise `NameError` (they
were a `TypeError` on `nil` and a value read from the wrong frame), and that a
module name stays visible when the enclosing function declares the same name
only later. Before the fix the three later-declaration checks fail.

The rule has a consequence to accept knowingly: two nested functions that call
each other cannot both be declared directly; one has to be declared as a
variable first (`var is_odd = nil`) and assigned after. At module level mutual
recursion keeps working.

<details><summary>Original report</summary>

The rule in Orbit's function scopes (user, 2026-10-19): a name is visible
only after its declaration. A name used before it is declared is looked up in
the enclosing scopes, out to the module; if it is nowhere, it is an error.
Modules are the exception by nature: their names are keys looked up at run
time, so a forward reference at module level works, and is expected to.

Inside functions the rule is broken. When a nested function uses a name that
the enclosing function declares **later**, the name is not declared yet at
the point of use, so `LookupInsert` makes it an `UNKNOWN` symbol of the nested
scope. `ComputeLocalVarOffset` then resolves it anyway:

```cpp
if (symbol->type == SymbolType::UNKNOWN) {
    if (scope->type != ScopeType::MODULE) {
        auto *tmp = LookupInEnclosing(symbol->name, scope);
        if (tmp != nullptr) {
            symbol->alias = tmp;   // also for a later declaration in a function
            continue;
        }
    }
    symbol->location = StorageLocation::GLOBAL;
}
```

`LookupInEnclosing` finds the later declaration, and the alias makes the
nested function use it. Nothing turns it into a capture either, so the
declaration stays a stack local of the enclosing function and the nested
function reads **its own** frame at that slot:

- a sibling declared later is called as `nil` (`TypeError`: "invalid call to
  a non-callable object('nil')");
- a local declared later reads whatever the nested frame holds (`34` in the
  PoC).

A name declared nowhere already behaves as the rule wants: it falls through to
`GLOBAL` and raises `NameError: name '...' is not defined` at run time.

**Found** while verifying SYM-002: its "function declared after the one that
captures it" check failed for this reason. Present before the closure work
(reproduced on `33611ce`).

**PoC:** [`poc/symtable/sym-003.orb`](poc/symtable/sym-003.orb)
*(confirmed live: the controls pass; the three later declarations give a
`TypeError` or a wrong value instead of `NameError`)*. It also pins down that a
module name stays visible when the enclosing function declares the same name
only later.

</details>

---

## SYM-002 — A variable captured two levels down reaches the innermost function through an unset closure
**Severity:** High (SIGSEGV on a nested closure) · **Status:** FIXED (2026-10-19) · **Location:** `orbit/liftoff/symtable.cpp:626` (`LookupInsert`), `orbit/liftoff/symtable.cpp` (`ComputeClosureDepth`), `orbit/orbiter/datatype/closure.cpp` (`ClosureGet`, `ClosureSet`), `orbit/orbiter/vm.cpp` (`CLONEW`, `CLOLDR`, `CLOSTR`)

**Fix verified (PoC `poc/symtable/sym-002.orb` — prints `ALL TESTS PASSED`,
9 of 9; `ortest` 24/26 and the PoC gate unchanged apart from open findings;
`phi-regalloc.orb` green).** Closures are now linked: each one keeps the
closure of the function around it, and a captured variable is addressed by
(depth, index).

- `LookupInsert` flags every function scope between the one that reads a
  captured variable and the one that declares it, so each of them hands a
  closure down.
- `Closure` has a `parent`, set by `CLONEW` to the closure the running
  function was created with (`this_func->closure`), and `ClosureTrace` visits
  it. A function that creates its own closure keeps it in its frame's closure
  slot and no longer loads the received one over it (`visitFunction`); a
  middle function with no captured variables of its own still passes the
  received closure through with `LDCLO`.
- Each function numbers only its own captured variables, from 0.
- `SymbolTable::ComputeClosureDepth` counts, from the reading scope up to the
  declaring one (excluded), the function scopes that create a closure
  (`ShouldCreateClosure()`, the predicate the prologue uses).
- `CLOLDR` and `CLOSTR` carry the depth in their 4 formerly reserved bits
  (`opcode.h`); in the IR it travels in `OffsetInstruction::depth`, an alias
  of `r_base`, so `LoadFromOffset` and `FindAndCreateAppropriateLoad` keep
  telling (depth, index) pairs apart. `ClosureGet`/`ClosureSet` follow
  `depth` parent links, then use `index`.
- A depth above `kClosureMaxDepth` (15) is caught by an `assert` in
  `LoadVariable`/`StoreVariable`: 16 nested levels of closures (depth 15)
  give the right result, 17 used to wrap silently to depth 0 (169 instead of
  153) and now stop on the assert. A proper compile error waits for the
  compiler to be able to report one.

The PoC covers two- and three-level captures of a local, a parameter and
`self`, one value from each of two levels, a middle function called twice
whose parameters and locals each closure must keep (`(1, 2)`, `(110, 120)`),
and a recursive middle function. With only the `LookupInsert` change those
last checks fail (`20`, `(0, 0)`, `(40, 40)`, and the recursion), which is what
made the linked layout necessary: a flat closure shared through the levels
gives every call of a middle function the same slot.

Two checks that were in this PoC belonged to other defects and moved: a
function declared after the one that captures it is SYM-003 (a forward
reference to an enclosing declaration, present before this work), and a write
two levels down not seen by the declaring function is IR-014.

**Note (2026-10-19):** an intermediate version gave every captured variable
of the nested functions a slot in the first enclosing closure
(`closure_target` in `ComputeLocalVarOffset`); it passed the original checks
but shared one slot across calls of a middle function, and its layout depended
on declaration order. It was replaced by the linked closures above.

<details><summary>Original report</summary>

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
method's class: a closure two levels inside a method crashed for this reason.

**PoC:** [`poc/symtable/sym-002.orb`](poc/symtable/sym-002.orb)
*(confirmed live on HEAD: the control passes, the first two-level capture
crashes)*.

</details>

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
