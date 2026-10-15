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
| [ir.md](ir.md) | `orbit/liftoff/ir` (linearscan, intervalspiller, irbuilder, instruction) | 2/10 |
| [compiler.md](compiler.md) | `orbit/liftoff/compiler.cpp` (compile driver) | 1/1 |
| [ctbuilder.md](ctbuilder.md) | `orbit/orbiter/datatype/ctbuilder.cpp` (class types, blueprint, hook dispatch) | 0/1 |
| [function.md](function.md) | `orbit/orbiter/datatype/function.cpp` (Function objects, `FuncShared` lifetime) | 1/1 |
| [oobject.md](oobject.md) | `orbit/orbiter/datatype/oobject.cpp` (object/type core, type lifecycle, type names) | 1/3 |
| [vm.md](vm.md) | `orbit/orbiter` (interpreter: trap unwind, registers, stack) | 1/9 |
| [number.md](number.md) | `orbit/orbiter/datatype/number.cpp` (integer literals & representation) | 2/2 |

## Top priorities (High severity, quick wins)

- ~~**PARSE-001** — `continue` directly inside a loop is rejected (CheckExt skips current context)~~ *(FIXED 2026-06-15)*
- ~~**PARSE-002** — empty doc comment `/*!*/` segfaults the compiler~~ *(FIXED 2026-07-27, empty doc buffer treated as no docstring)*
- ~~**IR-002** — `trap new X()` asserts in `AddInstructionBefore` (head-insert miscompile)~~ *(FIXED 2026-07-13)*
- ~~**IR-003** — two call results live at once collide in R13 → silent miscompile (`a+b` becomes `b+b`)~~ *(FIXED 2026-07-17, allocator restructured: CallerSaveSpiller pre-pass + IntervalSpiller)*
- ~~**IR-004** — a derived class resolved its own members through its superclass (wrong `init`, shadowed properties)~~ *(FIXED 2026-07-17, `LoadFromObjectProp` searches a class's own chain first)*
- ~~**IR-005** — instantiating a class three levels deep in an inheritance chain hangs the interpreter~~ *(FIXED 2026-07-22, `super` resolves from the enclosing class, not the receiver's runtime type)*
- ~~**IR-006** — two calls passing rest/named/keyword arguments collide on R10/R11/R12 (crash or silently wrong rest list)~~ *(FIXED 2026-07-24, the value is copied into the protocol register right before the call instead of the producer being pinned to it)*
- **IR-010** — any block (`if`, `for`) nested in a `catch` body aborts the compiler; root cause PARSE-019 (scope ranges overlap the next token)
- ~~**OBJ-002** — `GetTypeName` writes its NUL one byte past a `char[24]`: any type error on an instance of a class named with 24+ characters aborts (stack overflow)~~ *(FIXED 2026-10-15, the copy leaves room for the terminator; names past 23 characters are cut short)*
- **VM-009** — a `try`/`trap` context keeps an absolute address into the VM stack, which `realloc` moves: a `trap` in every frame of a recursion ~1,000 deep segfaults
- ~~**VM-007** — past about 510,000 frames every call reallocates the whole stack, so a bottomless recursion hangs for minutes before its overflow error; fix verified by probe~~ *(FIXED 2026-10-15, the last growth takes everything up to the limit in one step)*
- ~~**VM-008** — `PUSHIF` reads a type object as a Function when deciding whether to push the receiver (latent, by inspection)~~ *(FIXED 2026-10-15, `PUSHIF` asks the resolved constructor; confirmed live on `Error`)*
- ~~**VM-006** — `!x` disagrees with `if x`: `NOT` treats every integer as false and every heap object as true, so `if !3` is taken~~ *(FIXED 2026-10-14, `NOT` asks `IsTrue` like the conditional jumps)*
- ~~**VM-005** — integer literals of 2^32 and more load as their value shifted right by 32 (`LDIMM` shifts a 32-bit chunk by 32 or 48); one-line fix verified by probe~~ *(FIXED 2026-10-14, the `LDIMM` chunk is widened to 64 bits before it is shifted)*
- ~~**VM-004** — a call refused while binding its arguments leaves them on the stack: wrong arguments under `trap` in argument position, unbounded stack growth in a loop~~ *(FIXED 2026-10-14, `Bind` restores SP on every refusal; PoC covers fourteen shapes and four loops)*
- **COMP-001** — any syntax error in file mode asserts in `Compile` instead of reporting (release: UB on empty AST)
- **IR-007** — a blank (`_`) read as a value asserts instead of reporting (release: null deref); blocked by COMP-001
- ~~**VM-002** — a deferred control transfer in argument position overwrites the enclosing call's arguments~~ *(FIXED 2026-10-01, the call frame prologue now records the SP to restore instead of every exit path counting slots to pop)*
- ~~**VM-003** — `break` out of a `try` block is silently ignored, the loop runs to completion~~ *(FIXED 2026-10-05, an unlabelled jump target resolved to the try block instead of the loop)*
- ~~**IR-008** — `continue` out of a `try` block asserts in `GetJBlockBegin` at compile time~~ *(FIXED 2026-10-05, same cause as VM-003)*
- ~~**IR-009** — `AddInstructionFirst` dereferenced a null head, so a `try` as the first statement of a loop crashed codegen~~ *(FIXED 2026-10-05, sibling of IR-002 missed by its fix)*
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
- 2026-10-05: `orbit/liftoff/ir` + `orbit/orbiter` (abrupt exit from try/catch/finally) — VM-003 and IR-008 FIXED: they were one defect, an unlabelled `break`/`continue` target resolving to the enclosing TCF block instead of the loop, which also meant no `TSPA` was ever emitted. Two more fixed in the same pass: `TEND`'s propagation re-entered its own finally (infinite loop), and `ExPtrContainer::SetAction` OR-ed actions together instead of replacing them. IR-009 filed and FIXED, found through this work but unrelated to it. New `ortest/exceptions/` topic plus two regression PoCs; both fixes mutation-tested.
- 2026-10-13: `orbit/orbiter` (call binding) — VM-004 filed, confirmed live with [`poc/vm/trap-bind-error-argpos.orb`](poc/vm/trap-bind-error-argpos.orb). Found while writing the `ortest/net/` UDP suites. **The PoC gate is 35/36 until this is fixed**, per GUIDE §11.
- 2026-10-13: `orbit/orbiter` (`LDIMM` decoding) — VM-005 filed, confirmed live with [`poc/vm/ldimm-wide-literal.orb`](poc/vm/ldimm-wide-literal.orb) and the cause confirmed by probing the fix (then reverted). Found while testing `::orbit::net` bind flags. The PoC gate is 35/37 until VM-004 and VM-005 are fixed.
- 2026-10-13: `orbit/orbiter` (`NOT`) — VM-006 filed, confirmed live with [`poc/vm/not-truthiness.orb`](poc/vm/not-truthiness.orb). Found while writing the IPV6ONLY checks of `ortest/net/`. The PoC gate is 35/38 until VM-004, VM-005 and VM-006 are fixed.
- 2026-10-14: `orbit/orbiter` (`LDIMM` decoding) — VM-005 FIXED: the chunk is widened to `MSize` before the shift. PoC 7/7, `ortest` 22/22, SMI-range edges checked by hand; `ortest/net/02` and `04` use the `0x100000001` literal again. The PoC gate is 36/38 until VM-004 and VM-006 are fixed.
- 2026-10-14: `orbit/orbiter` (`NOT`) — VM-006 FIXED: `NOT` is `!IsTrue(value)`. PoC 13/13, `ortest` 22/22; `!=`/`!==` (lowered to `NOT(EQ)`) and every `!` in the stdlib checked, none relied on the defect. The PoC gate is 37/38 until VM-004 is fixed.
- 2026-10-14: `orbit/orbiter` (call binding, stack) + `orbit/liftoff/ir` (catch scopes) — VM-004 FIXED: `Bind` restores SP on every refusal, verified with a PoC rewritten to cover every refusal reachable from Orbit (fourteen shapes in argument position, four same-frame loops), which fails on the old binder and passes on the new one. Filed on the way: VM-007 (stack growth near the limit, fix verified by probe), VM-008 (`PUSHIF` type confusion, by inspection), IR-010 (blocks in a catch abort the compiler, root cause PARSE-019, confirmed under lldb). The PoC gate is 38/40 until VM-007 and IR-010 are fixed.
- 2026-10-15: `orbit/orbiter` (`PUSHIF`) — VM-008 FIXED: the method test uses the callable `ResolveCallable` returns. New PoC `poc/vm/pushif-type-target.orb` (every builtin type with a constructor, called through a selector) passes 11/11, and on the old code fails on `Error`, which confirms the defect live. The PoC gate is 39/41 until VM-007 and IR-010 are fixed.
- 2026-10-15: `orbit/orbiter` (stack) — VM-007 FIXED: `Grow` takes the rest of the limit in one step; PoC passes in 1.2 s, maximum depth unchanged. VM-009 filed while checking it (exception contexts dangle after the stack moves; SIGSEGV confirmed live, CP outside the live buffer confirmed under lldb), unrelated to the fix. The PoC gate is 40/42 until IR-010 and VM-009 are fixed.
- 2026-10-15: `orbit/orbiter/datatype/oobject.cpp` (`GetTypeName`) — OBJ-003 filed (booleans named 'nil' in Results and error messages, no BOOLEAN primitive) and, in the same function, OBJ-002 (off-by-one NUL past the caller's buffer, SIGABRT via `__stack_chk_fail` with a 24-character class name; fix verified by probe). New PoC directory `poc/oobject/`. The PoC gate is 40/44 until IR-010, VM-009, OBJ-002 and OBJ-003 are fixed.
- 2026-10-15: `orbit/orbiter/datatype/oobject.cpp` (`GetTypeName`) — OBJ-002 FIXED: `std::min(length, out_size - 1)`. PoC 5/5, no abort at any name length; every caller checked for a zero size and for use of the return value. The PoC gate is 41/44 until IR-010, VM-009 and OBJ-003 are fixed.
- 2026-10-15: `orbit/orbiter/datatype/oobject.cpp` (`GetTypeName`) — OBJ-003 FIXED: both booleans are named `Bool`, `nil` keeps `nil` on purpose. PoC 7/7 (pins both names), no suite relied on the old text. The PoC gate is 42/44 until IR-010 and VM-009 are fixed.
