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
| [parser.md](parser.md) | `orbit/liftoff/parser` (parser.cpp, context.h, ast.h) | 24/25 |
| [ir.md](ir.md) | `orbit/liftoff/ir` (linearscan, intervalspiller, irbuilder, instruction) | 0/5 |
| [compiler.md](compiler.md) | `orbit/liftoff/compiler.cpp` (compile driver) | 1/1 |
| [ctbuilder.md](ctbuilder.md) | `orbit/orbiter/datatype/ctbuilder.cpp` (class types, blueprint, hook dispatch) | 0/1 |
| [vm.md](vm.md) | `orbit/orbiter` (interpreter: trap unwind, registers) | 0/1 |
| [number.md](number.md) | `orbit/orbiter/datatype/number.cpp` (integer literals & representation) | 2/2 |

## Top priorities (High severity, quick wins)

- ~~**PARSE-001** — `continue` directly inside a loop is rejected (CheckExt skips current context)~~ *(FIXED 2026-06-15)*
- **PARSE-002** — empty doc comment `/*!*/` segfaults the compiler *(confirmed live)*
- ~~**IR-002** — `trap new X()` asserts in `AddInstructionBefore` (head-insert miscompile)~~ *(FIXED 2026-07-13)*
- ~~**IR-003** — two call results live at once collide in R13 → silent miscompile (`a+b` becomes `b+b`)~~ *(FIXED 2026-07-17, allocator restructured: CallerSaveSpiller pre-pass + IntervalSpiller)*
- ~~**IR-004** — a derived class resolved its own members through its superclass (wrong `init`, shadowed properties)~~ *(FIXED 2026-07-17, `LoadFromObjectProp` searches a class's own chain first)*
- ~~**IR-005** — instantiating a class three levels deep in an inheritance chain hangs the interpreter~~ *(FIXED 2026-07-22, `super` resolves from the enclosing class, not the receiver's runtime type)*
- **COMP-001** — any syntax error in file mode asserts in `Compile` instead of reporting (release: UB on empty AST)
- ~~**VM-001** — spill slots clobbered after a trapped panic (SP rewound to end of exception block)~~ *(FIXED 2026-07-08)*
- ~~**SCAN-001** — `"#..."` string literals mis-lexed (hash counting on non-raw strings)~~ *(FIXED 2026-06-13)*
- ~~**SCAN-002** — empty `#` comment swallows the newline + next line of code~~ *(FIXED 2026-06-13)*
- ~~**SCAN-003** — octal escapes with zero digits decode wrong (`\100` → 1)~~ *(FIXED 2026-06-13, incl. overflow check)*
- ~~**UTF8-001** — `\u` escapes produce invalid UTF-8 for most Cyrillic / Latin-Ext-A~~ *(FIXED 2026-06-13)*
- **IBUF-001** — `GetCurrentLine` OOB read + `size_t` underflow → crash (latent, no callers yet)

## Reviewed so far

- 2026-06-12: `orbit/liftoff/scanner` (all files) + UTF-8 helpers it depends on.
- 2026-06-12: `orbit/liftoff/parser` (all files); PARSE-001/002/003/004/005 + SCAN-001 reproduced against `bin/Orbit`.
- 2026-07-15: `orbit/liftoff/ir/linearscan.cpp` (register allocator); IR-003 filed (R13 cross-call miscompile) with tiered `ortest/regalloc_*.orb` coverage. A separate register-leak segfault in `SpillAndAssignRegister` was fixed in the working tree (not yet committed).
- 2026-07-17: allocator restructured (CallerSaveSpiller pre-pass + IntervalSpiller extraction + LinearScan contention hardening); IR-003 verified FIXED — full PoC suite 20/20, `ortest/regalloc_01..05` all green.
- 2026-07-17: class/inheritance machinery (`LoadFromObjectProp`, `ctbuilder.cpp`); IR-004 and CTB-001 filed and FIXED, IR-005 filed OPEN. New `ortest/oop/` topic (4 suites) covers hooks, inheritance resolution, accessor/method namespace separation and type-object receivers.
