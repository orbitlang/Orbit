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
| [ir.md](ir.md) | `orbit/liftoff/ir` (linearscan, irbuilder, instruction) | 1/2 |
| [compiler.md](compiler.md) | `orbit/liftoff/compiler.cpp` (compile driver) | 1/1 |
| [number.md](number.md) | `orbit/orbiter/datatype/number.cpp` (integer literals & representation) | 2/2 |

## Top priorities (High severity, quick wins)

- ~~**PARSE-001** — `continue` directly inside a loop is rejected (CheckExt skips current context)~~ *(FIXED 2026-06-15)*
- **PARSE-002** — empty doc comment `/*!*/` segfaults the compiler *(confirmed live)*
- **IR-002** — `trap new X()` asserts in `AddInstructionBefore` (compile-time crash, 1-line repro)
- **COMP-001** — any syntax error in file mode asserts in `Compile` instead of reporting (release: UB on empty AST)
- ~~**SCAN-001** — `"#..."` string literals mis-lexed (hash counting on non-raw strings)~~ *(FIXED 2026-06-13)*
- ~~**SCAN-002** — empty `#` comment swallows the newline + next line of code~~ *(FIXED 2026-06-13)*
- ~~**SCAN-003** — octal escapes with zero digits decode wrong (`\100` → 1)~~ *(FIXED 2026-06-13, incl. overflow check)*
- ~~**UTF8-001** — `\u` escapes produce invalid UTF-8 for most Cyrillic / Latin-Ext-A~~ *(FIXED 2026-06-13)*
- **IBUF-001** — `GetCurrentLine` OOB read + `size_t` underflow → crash (latent, no callers yet)

## Reviewed so far

- 2026-06-12: `orbit/liftoff/scanner` (all files) + UTF-8 helpers it depends on.
- 2026-06-12: `orbit/liftoff/parser` (all files); PARSE-001/002/003/004/005 + SCAN-001 reproduced against `bin/Orbit`.
