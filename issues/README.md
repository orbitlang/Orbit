# issues/ — Project issue tracking

Component-by-component tracker for bugs, defects, and known issues across the
project. One file per component; each finding has a stable ID, severity, and a
**Status** field (OPEN / PARTIAL / FIXED / WONTFIX) that is updated as issues
are resolved.

> **Conventions:** see [GUIDE.md](GUIDE.md) for how to create, update, and
> manage these reports (IDs, statuses, verification, and multi-agent rules).
> Read it before adding or modifying findings.
>
> **Regression PoCs:** runnable reproducers live in [`poc/`](poc/). After
> rebuilding, run `issues/poc/run.sh [component]` to confirm fixed bugs haven't
> regressed.

## Index

| File | Component | Findings (open/total) |
|---|---|---|
| [scanner.md](scanner.md) | `orbit/liftoff/scanner` (scanner.cpp, token.h) | 1/19 (+1 WONTFIX) |
| [ibuffer.md](ibuffer.md) | `orbit/liftoff/scanner/ibuffer` | 2/7 |
| [sbuffer.md](sbuffer.md) | `orbit/liftoff/scanner/sbuffer` | 0/3 |
| [utf8-stringbuilder.md](utf8-stringbuilder.md) | `orbit/orbiter/datatype/stringbuilder` (UTF-8 codec) | 1/4 |
| [parser.md](parser.md) | `orbit/liftoff/parser` (parser.cpp, context.h, ast.h) | 13/25 |
| [ir.md](ir.md) | `orbit/liftoff/ir` (linearscan, intervalspiller, irbuilder, instruction) | 2/8 |
| [compiler.md](compiler.md) | `orbit/liftoff/compiler.cpp` (compile driver) | 1/1 |
| [ctbuilder.md](ctbuilder.md) | `orbit/orbiter/datatype/ctbuilder.cpp` (class types, blueprint, hook dispatch) | 0/1 |
| [function.md](function.md) | `orbit/orbiter/datatype/function.cpp` (Function objects, `FuncShared` lifetime) | 1/1 |
| [oobject.md](oobject.md) | `orbit/orbiter/datatype/oobject.cpp` (object/type core, type lifecycle) | 1/1 |
| [vm.md](vm.md) | `orbit/orbiter` (interpreter: trap unwind, registers) | 1/3 |
| [number.md](number.md) | `orbit/orbiter/datatype/number.cpp` (integer literals & representation) | 2/2 |

## Top priorities (High severity, quick wins)

- ~~**PARSE-001** — `continue` directly inside a loop is rejected (CheckExt skips current context)~~ *(FIXED 2026-06-15)*
- ~~**PARSE-002** — empty doc comment `/*!*/` segfaults the compiler~~ *(FIXED 2026-07-27, empty doc buffer treated as no docstring)*
- ~~**IR-002** — `trap new X()` asserts in `AddInstructionBefore` (head-insert miscompile)~~ *(FIXED 2026-07-13)*
- ~~**IR-003** — two call results live at once collide in R13 → silent miscompile (`a+b` becomes `b+b`)~~ *(FIXED 2026-07-17, allocator restructured: CallerSaveSpiller pre-pass + IntervalSpiller)*
- ~~**IR-004** — a derived class resolved its own members through its superclass (wrong `init`, shadowed properties)~~ *(FIXED 2026-07-17, `LoadFromObjectProp` searches a class's own chain first)*
- ~~**IR-005** — instantiating a class three levels deep in an inheritance chain hangs the interpreter~~ *(FIXED 2026-07-22, `super` resolves from the enclosing class, not the receiver's runtime type)*
- ~~**IR-006** — two calls passing rest/named/keyword arguments collide on R10/R11/R12 (crash or silently wrong rest list)~~ *(FIXED 2026-07-24, the value is copied into the protocol register right before the call instead of the producer being pinned to it)*
- **COMP-001** — any syntax error in file mode asserts in `Compile` instead of reporting (release: UB on empty AST)
- **IR-007** — a blank (`_`) read as a value asserts instead of reporting (release: null deref); blocked by COMP-001
- ~~**VM-002** — a deferred control transfer in argument position overwrites the enclosing call's arguments~~ *(FIXED 2026-10-01, the call frame prologue now records the SP to restore instead of every exit path counting slots to pop)*
- **VM-003** — `break` out of a `try` block is silently ignored, the loop runs to completion
- **IR-008** — `continue` out of a `try` block asserts in `GetJBlockBegin` at compile time
- ~~**VM-001** — spill slots clobbered after a trapped panic (SP rewound to end of exception block)~~ *(FIXED 2026-07-08)*
- ~~**SCAN-001** — `"#..."` string literals mis-lexed (hash counting on non-raw strings)~~ *(FIXED 2026-06-13)*
- ~~**SCAN-002** — empty `#` comment swallows the newline + next line of code~~ *(FIXED 2026-06-13)*
- ~~**SCAN-003** — octal escapes with zero digits decode wrong (`\100` → 1)~~ *(FIXED 2026-06-13, incl. overflow check)*
- ~~**UTF8-001** — `\u` escapes produce invalid UTF-8 for most Cyrillic / Latin-Ext-A~~ *(FIXED 2026-06-13)*
- **IBUF-001** — `GetCurrentLine` OOB read + `size_t` underflow → crash (latent, no callers yet)
- **OBJ-001** — no TypeInfo destructor: a collected type leaks its name, property table and `aux.data` (`aux.dtor` is never called), and pins every property value forever

## Reviewed so far

- 2026-06-12: `orbit/liftoff/scanner` (all files) + UTF-8 helpers it depends on.
- 2026-06-12: `orbit/liftoff/parser` (all files); PARSE-001/002/003/004/005 + SCAN-001 reproduced against `bin/Orbit`.
- 2026-07-15: `orbit/liftoff/ir/linearscan.cpp` (register allocator); IR-003 filed (R13 cross-call miscompile) with tiered `ortest/regalloc_*.orb` coverage. A separate register-leak segfault in `SpillAndAssignRegister` was fixed in the working tree (not yet committed).
- 2026-07-17: allocator restructured (CallerSaveSpiller pre-pass + IntervalSpiller extraction + LinearScan contention hardening); IR-003 verified FIXED — full PoC suite 20/20, `ortest/regalloc_01..05` all green.
- 2026-07-17: class/inheritance machinery (`LoadFromObjectProp`, `ctbuilder.cpp`); IR-004 and CTB-001 filed and FIXED, IR-005 filed OPEN. New `ortest/oop/` topic (4 suites) covers hooks, inheritance resolution, accessor/method namespace separation and type-object receivers.
- 2026-08-31: `orbit/liftoff/parser` — verified & closed PARSE-003/004/005/007/008/011/012/016/018/020 (parser open 23→13). 003/004/005/007/012/018/020 confirmed live against `bin/Orbit` with new `poc/parser/parse-*.orb` (gate 9/9); 008/011/016 by inspection. `pub import` confirmed valid (PARSE-020).
- 2026-09-23: `orbit/orbiter/datatype/function.cpp` (Function / `FuncShared` lifetime) — new component; FUNC-001 filed (strong module/owner_type backpointers close a refcount cycle the collector cannot break).
- 2026-09-23: `orbit/orbiter/datatype/oobject.cpp` (type lifecycle) — new component; OBJ-001 filed (no type destructor, `aux.dtor` never invoked) while reviewing the module/function rework that moved native module functions into the module instance slots.
- 2026-09-29: `orbit/orbiter` (trap unwind) — VM-002 filed, confirmed live with [`poc/vm/trap-argpos-clobber.orb`](poc/vm/trap-argpos-clobber.orb). Found while writing the `ortest/net/` suites, whose refusal checks all had the shape `check_error(name, trap expr, ...)`. **The PoC gate is 32/33 until this is fixed**, per GUIDE §11 (an OPEN finding's PoC documents the target behavior).
- 2026-09-30: `orbit/liftoff/ir` (irbuilder, blank targets) — IR-007 filed while verifying the new `_` support. Destructuring, single-target discards and `for var _ in` all verified green; reading `_` as a value is the remaining hole and waits on COMP-001, since the compiler has no diagnostic path to report it with.
- 2026-09-30: `orbit/orbiter` (exception stack) — `ExceptionContext` gained an explicit `SP`, which closes the trapped-panic half of VM-002 (PoC's `trap` checks all green); the `return`-through-`finally` half is still open, so VM-002 is PARTIAL and its PoC now covers both. Confirmed by building HEAD and the working tree and running the same reproducers: VM-003 (`break` out of a `try`) and IR-008 (`continue` out of a `try`) predate that work and are filed as their own findings. The now-unwritten `ret_pops` field is noted under VM-002.
- 2026-10-01: `orbit/orbiter` (call frames) — the frame prologue now carries the stack pointer to restore (`Context | SP | BP | IP`), sourced from `ArgumentBinder::ArgsBaseSP()`, and `Return` no longer pops by count. VM-002 FIXED as a result, both halves, with the PoC green and `ortest` 19/19. Two latent discrepancies surfaced on the way and are now documented on `ArgsBaseSP`: the binder pushes slots it does not count (rest list, kwargs dict), and drops the receiver of a method-mode call whose callee is not a method. VM-003 and IR-008 (`break` and `continue` out of a `try`) are untouched and still open.
