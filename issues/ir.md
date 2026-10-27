# ir — bug report

**Component:** `orbit/liftoff/ir` (instruction.h, linearscan.cpp, irbuilder.cpp) · **ID prefix:** `IR`
**PoCs:** [`poc/ir/`](poc/ir/) · **Last reviewed:** 2026-10-27
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

> Imported 2026-06-15 from `orbit/liftoff/ir/KNOWN_ISSUES.md` (ISSUE-001 → IR-001).

---

## IR-019 — A block in a `when` branch leaves the IR in the wrong scope
**Severity:** High (compile-time abort on valid source) · **Status:** FIXED (2026-10-27) · **Location:** `orbit/liftoff/parser/parser.cpp:1259` (`ParseBlock`, declarations only), `:1069` / `:1086` (`ParseWhenStatement`)

**Fixed by restricting `when` (verified live: PoC
`poc/ir/ir-019-block-in-when.orb`, rewritten as an error test, is refused with
"Invalid statement in a 'when' branch" instead of reaching the IR builder; the
new suites `ortest/when/01_declarations.orb`, `02_no_control_flow.orb` and
`03_no_expressions.orb` pass, and the two refusal suites fail with the check
removed; `ortest` 34/34, the PoC gate 63/64 with the open VM-010,
`phi-regalloc.orb` green, every `stdlib` module imports).** A `when` branch now
holds declarations only, as the user decided: `ParseWhenStatement` parses each
branch with `ParseBlock(false, true)`, which refuses any statement that does
not start like a declaration (`var`, `let`, `func`, `class`, `trait`,
`native`, `import`, a decorator, `pub`, a nested `when`, or an identifier or
`_` for `:=`) and, once parsed, any expression statement or labeled loop:

```cpp
} else if (declarations_only) {
    if (!this->Match(TokenType::BLANK, TokenType::DECORATOR, TokenType::IDENTIFIER, TokenType::KW_CLASS,
                     TokenType::KW_FUNC, TokenType::KW_IMPORT, TokenType::KW_LET, TokenType::KW_NATIVE,
                     TokenType::KW_PUB, TokenType::KW_TRAIT, TokenType::KW_VAR, TokenType::KW_WHEN))
        throw ParserException(103);
}

auto stmt = this->ParseStatement();

if (declarations_only && (stmt->is_expr || stmt->node_type == NodeType::LABEL))
    throw ParserException(103);
```

A declaration opens no nested scope in the branch (functions and classes are
named scopes, entered by name), so `visitWhen` has nothing to enter and the
abort cannot happen. Code with control flow goes in a function declared in
the branches; the suite checks such a function, followed by an `if` after
the `when`. Also checked by probe: eighteen statements refused (`if`, `loop`,
`for`, `switch`, `try`, `sync`, `defer`, `return`, a labeled loop, an
assignment, `+=`, a call, an anonymous function, a bare name or literal, in
the `else` branch and in a nested `when`). The grammar says the same
(`when_block = "{", {module_declaration}, "}"`).

Found on the way: PARSE-027 (`:=` of the same name in two branches, or any
shadowing `:=`, is never read).

<details><summary>Original report</summary>

An `if` in a `when` branch followed by another `if`, after the `when` or in
its `else`, aborts the compiler:

```orbit
when runtime.os != "" {
    if true { a = 1 }
}
if true { a += 10 }
```

```
Assertion failed: (false), function Generate, file irbuilder.cpp, line 2102.
```

`visitWhen` visits the branches without entering their scopes, which the
parser creates (`ParseWhenStatement`) and merges into the module's
(`LeaveMergeNestedScope`). For the inner `if`, `EnterNestedScope` walks from
the module down through the `when`'s scope to the `if`'s; the matching
`LeaveNestedScope` climbs one level, to the `when`'s scope, not to the module.
The next `if` lies outside that scope and is not found. An `if` alone in a
`when`, or any number of them followed by nothing that opens a scope, gets
through.

**Found** while checking that the `when` scopes now close at their blocks'
ends (PARSE-019's audit); present before that change too.

**PoC:** [`poc/ir/ir-019-block-in-when.orb`](poc/ir/ir-019-block-in-when.orb)
*(confirmed live: the compile aborts)*.

**Fix (suggested, verified by probe 2026-10-27, then reverted):** enter and
leave the branch's scope around each branch, as `visitBranch` does for an
`if`: `EnterNestedScope(node->body->loc.start.offset)` /
`LeaveNestedScope()` around the body, and the same around an `else` block
(an `else when` recurses). With the probe both checks of the PoC pass,
`stdlib/readline.orb` (a `when` / `else`) compiles, `ortest` is 31/31 and the
PoC gate is unchanged.

**Direction (user, 2026-10-27):** rather than the fix above, restrict a
`when` branch to declarations (`var`, `let`, `:=`, `func`, `class`, `trait`,
`native`, `import`, decorators, `else when`), which is what `when` is for:
choosing which definitions exist. Control flow in a branch becomes a syntax
error with its own message, and this abort cannot happen. The PoC will then
be rewritten as an error test. Not scheduled yet.

</details>

---

## IR-018 — `continue` in a loop with a step skips the step, and the loop never ends
**Severity:** High (a hang on valid source) · **Status:** FIXED (2026-10-27) · **Location:** `orbit/liftoff/ir/irbuilder.cpp:1339` (`visitLoop`, the `init; cond; step` form)

**Fix verified (live: PoC `poc/ir/ir-018-continue-skips-step.orb` prints
`ALL TESTS PASSED`, 5 of 5, and reports both loops STUCK again with the old
`visitLoop` put back; `phi-regalloc.orb` green; `ortest` 31/31 and the PoC
gate 62/64, with the open IR-019 and VM-010, on five runs in a row; every
`stdlib` module imports).** The step now lives in the loop's `begin` block,
where `continue` lands, and the first iteration jumps past it to the
condition:

```cpp
auto *cond = this->builder_.CreateBasicBlock();

this->builder_.CreateJump(cond);

this->builder_.AppendBasicBlock(jb.begin);

if (node->inc != nullptr)
    this->visit(node->inc);

this->builder_.AppendBasicBlock(cond);
// test, JF end, body, JMP begin
```

A labeled loop shares its label's `begin` block, so `continue outer` runs the
outer loop's step with no change to `visitJump`. Also checked by probe: a step
that calls a closure, a closure made in the body, a loop that never runs,
`break` and `continue` together, nested loops with plain and labeled
`continue`, a labeled `break`, `continue` in a generator, a body that moves
the variable before `continue`, and the loop at module scope. The
`ir-004-derived-ctor-body.orb` failure seen once with the probe did not come
back in the five gate runs.

<details><summary>Original report</summary>

`continue` in a `loop init; cond; step` goes back to the condition without
running the step, so the loop variable never moves:

```orbit
loop i := 0; i < 3; i++ {
    if i == 1 { continue }    // i stays 1 forever
    r += i
}
```

`visitLoop` puts the condition in the loop's `begin` block and emits the step
after the body, in the same flow; `visitJump` sends a `continue` to
`GetJBlockBegin`, the condition. A labeled loop shares its label's `begin`
and `end` blocks (`JBlockTarget`'s constructor), so `continue outer` skips
the outer loop's step the same way. `break`, the `while`-style
`loop cond { }` and for-in loops are fine.

**Found** while checking that IR-010's fix holds for adjacent blocks: a loop
with a `continue` in the test never returned. Present on the code before that
fix too.

**PoC:** [`poc/ir/ir-018-continue-skips-step.orb`](poc/ir/ir-018-continue-skips-step.orb)
*(confirmed live: the three controls pass; the loop with a step and the
labeled one report STUCK, each loop giving up after 50 iterations instead of
hanging)*.

**Fix (suggested, probed 2026-10-27, then reverted):** put the step in the
`begin` block, where `continue` lands, and have the first iteration jump
past it:

```cpp
if (node->init != nullptr)
    this->visit(node->init);

auto *cond = this->builder_.CreateBasicBlock();
this->builder_.CreateJump(cond);                 // the first iteration skips the step

this->builder_.AppendBasicBlock(jb.begin);       // `continue` lands here, labeled or not
if (node->inc != nullptr)
    this->visit(node->inc);

this->builder_.AppendBasicBlock(cond);
// test, JF end, body, JMP begin as today
```

Labeled loops are covered with no change, since they share `begin`. With the
probe both checks pass and `ortest` is 31/31. One run of the PoC gate
reported `ir-004-derived-ctor-body.orb` failed; the PoC passed on its own and
five more runs of the IR PoCs passed, so it is unexplained rather than
reproduced: rerun the gate a few times when the fix lands.

</details>

---

## IR-017 — An `if` in a `switch` case aborts the compiler
**Severity:** High (compile-time abort on valid source) · **Status:** FIXED (2026-10-27) · **Location:** `orbit/liftoff/parser/parser.cpp:764` (`ParseSwitchCase`), `orbit/liftoff/ir/irbuilder.cpp:1584` (`visitSwitchBlock`)

**Fix verified (PoC `poc/ir/ir-017-if-in-switch-case.orb` — prints `ALL
TESTS PASSED`, 5 of 5; `ortest` 31/31 and the PoC gate 60/62, the failures
being IR-018 and VM-010; `phi-regalloc.orb` green).** A case has no closing
token, so its end is the end of its last statement (or of `fallthrough`), and
the colon's when the body is empty. The case's scope now spans the whole case,
from `case`/`default` to that end, and the IR enters it through the case's
first token:

```cpp
// ParseSwitchCase: opened after the tests, which belong to the enclosing scope
if (!this->sym_t_->CreateNestedScope(sw_case->loc.start.offset))
...
sw_case->loc.end = fallthrough || !block->statements.empty() ? block->loc.end : colon_end;
this->sym_t_->LeaveNestedScope(sw_case->loc.end.offset);

// visitSwitchBlock
if (!this->sym_t_->EnterNestedScope(case_node->loc.start.offset))
```

A scope that covers only the case's body would be empty for an empty case
(`case 1:` followed by `case 2:`), and that is not harmless:
`EnterNestedScope` with an offset no child contains stays in the active scope
and still succeeds, the matching `LeaveNestedScope` then leaves one level too
many, and the program crashes later (seen while fixing this: SIGSEGV on an
empty case). Starting the scope at `case` keeps it from ever being empty. The
PoC gained that case and a `fallthrough`.

<details><summary>Original report</summary>

An `if` anywhere in the body of a `case` or of the `default`, in a function
or at module scope, aborts the compile:

```orbit
switch v {
    case 1:
        if true { r = 1 }
}
```

```
Assertion failed: (false), function Generate, file irbuilder.cpp, line 2101.
```

`ParseSwitchCase` creates the case's scope at the first token after the
colon and closes it with `LeaveNestedScope(block->loc.end.offset)`, but
`block->loc.end` is never updated after the case's statements are read: it is
still the end of that first token. The case's scope covers that single token,
the `if`'s scope (its child) lies outside it, and `EnterNestedScope` cannot
reach it from the case's scope, so `visitBranch` throws
`SymbolTableException`. Plain statements in a case, a `for` in a case and an
`if` after the switch compile. It is the second point of PARSE-019 (the case's
`loc.end` is never updated), filed there as Info before its effect was known.

**Found** while checking that IR-010's fix holds for adjacent blocks. Present
on the code before that fix too.

**PoC:** [`poc/ir/ir-017-if-in-switch-case.orb`](poc/ir/ir-017-if-in-switch-case.orb)
*(confirmed live: the compile aborts)*.

**Fix (suggested, verified by probe 2026-10-27, then reverted):** set the end
of the case's block to the token that ends the case (the next `case`,
`default` or `}`) before closing its scope:

```cpp
block->loc.end = TKCUR_START;

this->sym_t_->LeaveNestedScope(block->loc.end.offset);
```

With it an `if` in a case and in the default compiles and runs, `ortest` is
31/31 and the PoC gate is unchanged.

</details>

---

## IR-016 — A value live across a `spawn` is not saved, and reads back as nil
**Severity:** High (silent nil; a SIGSEGV when the value is a channel) · **Status:** FIXED (2026-10-26) · **Location:** `orbit/liftoff/ir/ircontext.cpp:226` (`IRContext::CallerSaveSpiller`)

**Fix verified (PoC `poc/ir/ir-016-value-live-across-spawn.orb` — prints
`ALL TESTS PASSED`, 2 of 2; `ortest` 31/31 and the PoC gate 58/60, the
failures being open findings; `phi-regalloc.orb` green).**
`CallerSaveSpiller` counts `SPWN` among the instructions that clobber the
registers, next to `CALL`, `EXECSUB` and `NTCALL`, and its comment says why
(it binds its arguments like a call and sets `RR` to nil). A value live
across a spawn is saved to the stack before it and reloaded after. Without
the line the PoC fails both checks, the second as a crash. `DEFER` is left
out: its VM handler does not write `RR`.

<details><summary>Original report</summary>

`spawn f()` is compiled as a call whose opcode is then changed to `SPWN`, and
the VM's `SPWN` binds the arguments like a call and sets `RR`, the result
register, to nil. The caller-save spiller does not know: it saves the
registers live across `CALL`, `EXECSUB` and `NTCALL`, not `SPWN`. A value the
allocator keeps in `RR` across the spawn, typically the result of the call
just before it, reads back as nil:

```orbit
v := id(41)
spawn noop()
v + 1             // NotImplementedError: '+' between 'nil' and 'Number'

ch := Chan(capacity=1)
spawn producer()
<- ch             // SIGSEGV: ChannelRecv on a null channel (asyncrwlock.cpp:86)
```

**Found** while adding a channel case to the IR-014 PoC: the receive crashed
the process, with the builder of `HEAD` too, in `ChannelRecv(channel=0x0)`.

**PoC:** [`poc/ir/ir-016-value-live-across-spawn.orb`](poc/ir/ir-016-value-live-across-spawn.orb)
*(confirmed live: the call's result reads back as nil, then the receive
crashes)*.

**Fix (suggested, verified by probe 2026-10-26, then reverted):** add `SPWN`
to the instructions `CallerSaveSpiller` treats as clobbering the registers.
With it the PoC passes and the gates are unchanged. Whether `DEFER`, built
the same way, needs it too is open: its VM handler does not write `RR`.

</details>

---

## IR-015 — A generator that captures a variable is compiled as an ordinary function
**Severity:** High (wrong code; a SIGSEGV in the PoC) · **Status:** FIXED (2026-10-19) · **Location:** `orbit/liftoff/ir/irbuilder.cpp:1036` (`visitFunction`)

**Fix verified (PoC `poc/ir/ir-015-capturing-generator.orb` — prints
`ALL TESTS PASSED`, 4 of 4; `ortest` 24/26 and the PoC gate 50/56, the
failures being open findings; `phi-regalloc.orb` green).** The generator test
checks the bit instead of comparing the whole flag set:

```cpp
if (ENUMBITMASK_ISTRUE(node->symbol->defining_scope->flags, ScopeFlags::GENERATOR))
    f_flags |= orbiter::LoadFuncFlags::GENERATOR;
```

A generator that reads an enclosing variable, one in the middle of a
two-level capture and one nested in a method reading `self` are compiled as
generators again; with the old equality test the PoC crashes at the first of
them. It was the only `==`/`!=` comparison on `ScopeFlags` in
`orbit/liftoff`, and none is left.

<details><summary>Original report</summary>

`visitFunction` decides whether a function is a generator with an equality
test on its scope's flags:

```cpp
if (node->symbol->defining_scope->flags == ScopeFlags::GENERATOR)
    f_flags |= orbiter::LoadFuncFlags::GENERATOR;
```

`ScopeFlags` is a bit set. A generator that reads a variable of the function
around it also has `ScopeFlags::CLOSURE`, the equality fails, and the
generator is compiled as an ordinary function: its first `yield` returns the
value. `for x in gen()` then reports "'Number' object is not iterable", and
in the PoC's shape (the generator passed to a helper and iterated there) the
process crashes with SIGSEGV even under `trap`.

The one-level case predates the closure work, since `LookupInsert` always
flagged the reading scope `CLOSURE`. Since the SYM-002 partial fix every
function on the path of a capture is flagged, so a generator in the middle of
a two-level capture is hit as well. Generators nested in methods that read
`self` are hit too, `self` being a captured parameter.

**Found** while reviewing `ComputeClosureDepth` for SYM-002: it is the only
`flags ==` comparison on `ScopeFlags` in `orbit/liftoff`.

**PoC:** [`poc/ir/ir-015-capturing-generator.orb`](poc/ir/ir-015-capturing-generator.orb)
*(confirmed live: the control passes, the first capturing generator crashes)*.

</details>

---

## IR-014 — A captured variable read after a call returns the value stored before it
**Severity:** High (silent wrong result) · **Status:** FIXED (2026-10-26) · **Location:** `orbit/liftoff/ir/builder.cpp:16` (`MayRunOtherCode`), `:34` (`Builder::AddInstruction`), `:516` (`Builder::LoadFromOffset`)

**Fix verified (PoC `poc/ir/ir-014-closure-write-after-call.orb` — prints
`ALL TESTS PASSED`, 11 of 11; `ortest` 31/31 and the PoC gate 58/60, the
failures being open findings; `phi-regalloc.orb` green).** Both reuse
mechanisms of the IR builder now stop at the same set of instructions,
`MayRunOtherCode`: the ones that run other code (`CALL`, `NTCALL`,
`EXECSUB`) and the synchronization points, where the fiber waits while other
code runs (`YLD`, `AWAIT`, `CHRCV`, `CHSND`, `SYNC_ENTER`). After any of them
another piece of code may have changed any variable except the frame's own
slots.

1. `LoadFromOffset` stops its backward search at such an instruction for
   every opcode but `SKLDR`, so neither a store nor a load of a closure,
   module or global slot is reused across it:

   ```cpp
   if (opcode != OPCode::SKLDR && MayRunOtherCode(instr->opcode))
       break;
   ```

2. `AddInstruction` clears `IRContext::active_regs_`, the symbol-to-register
   cache `LoadVariable` consults first, whenever it appends such an
   instruction. Without this the module code kept reading a register loaded
   before the call (part 1 alone regressed `oop/02` and the IR-013 PoC, see
   the report).

`SPWN` is not in the set: reading a variable another fiber writes, with no
synchronization in between, is a race in the program, and closure variables
are never cached. A related defect found on the way, a value kept in `RR`
across a spawn, was IR-016.

The PoC covers closures (a write, a compound assignment, a read before and
after, a counter, a write two levels down, a read across a `yield`) and
module variables (bumped by two calls and read after each, copied before a
call, read after an `await`, read after a channel receive). The old code
fails nine checks; part 1 alone fails the first module case; stopping only at
`CALL`/`NTCALL` fails the `yield`, `await` and receive cases.

Known limit, by inspection: class hooks (`compare`, `equal`, `str`, `repr`,
`hash`) run Orbit code through `EvalSync` inside instructions outside the set
(comparisons, equality, hashing). A hook that changes a module variable the
calling code then reads is not covered; adding those instructions would drop
the cache at nearly every comparison.

<details><summary>Original report</summary>

`LoadFromOffset` walks the current basic block backwards looking for an
earlier access to the same slot. A matching **load** is reused only when no
call lies in between (the comment explains why: the callee may change the
slot). A matching **store** is forwarded unconditionally:

```cpp
if (last != nullptr && last->r_base == r_base && last->offset == offset && last->flags == flags)
    return (Instruction *) last->operands->value;   // no check of `call`
```

For a closure slot (`CLOSTR` then `CLOLDR`) the call in between is exactly how
a nested function changes the variable, so the function that declares it
keeps reading the value it stored itself:

```orbit
func counter() {
    var count = 0
    func tick() { count += 1 }
    tick()
    tick()
    tick()
    return count      // 0
}
```

The nested function does see the declaring function's writes, since it loads
from the closure.

*(2026-10-26)* Module variables have the same defect; it was hidden, not
absent. In module code a read is forwarded from a store across a call just
the same, and what made the probed cases come out right is the caller-save
spiller: a value live across a call whose next use is a `STGOFF` is reloaded
from that global slot (`intervalspiller.cpp:61`), which happens to give the
variable's current value. When the two differ, the module reads the wrong
one:

```orbit
func set_a() { a = 9 }

var a = 5
b := a
set_a()
io.print(a, b)    // 9 9, not 9 5
```

There is a second cache on top of `LoadFromOffset`: `IRContext::active_regs_`,
which `IRBuilder::LoadVariable` consults first and which maps a symbol to the
last load of it. It is cleared by a store to the symbol, a new basic block or
a new IR context, never by a call; closures are kept out of it
(`AddActiveVar`), module and global variables are not.

**Found** while verifying the SYM-002 fix: a write two levels down was not
seen by the declaring function, and the cause turned out to be one level.
Present before the closure fixes (reproduced on `33611ce`).

**PoC:** [`poc/ir/ir-014-closure-write-after-call.orb`](poc/ir/ir-014-closure-write-after-call.orb)
*(confirmed live: the control passes, the four reads after a call fail)*.
A fifth check, a write two levels down, moved here from the SYM-002 PoC on
2026-10-19; it fails the same way now that closures are linked.

**Fix (suggested, verified by probe 2026-10-26):** two parts, both needed,
built on one predicate, `MayRunOtherCode`: an instruction after which other
code may have changed any variable except the frame's own slots. That is
every instruction that runs other code (`CALL`, `NTCALL`, `EXECSUB`) and
every synchronization point, where the fiber waits and other code runs
before it goes on (`YLD`, `AWAIT`, `CHRCV`, `CHSND`, `SYNC_ENTER`). `AWAIT`
runs nothing itself, but `x = await f` must see what `f` wrote, and a channel
receive must see what the sender wrote before sending: without them the
module reads 0 instead of 5 after an `await`, and 0 instead of 7 after a
receive (both added to the PoC, which checks only the value read after them,
since the one read before depends on scheduling). `SPWN` is not needed:
reading a variable another fiber writes, with no synchronization in
between, is a race in the program, and closures are never cached anyway.

1. In `LoadFromOffset`, reuse only frame slots (`SKLDR`): a store or a load of
   a closure, module or global slot is never forwarded. A `yield` is not a
   call, so stopping at calls alone would miss a generator reading a captured
   variable on both sides of it (`[1, 1]` instead of `[1, 42]`, added to the
   PoC).
2. In `Builder::AddInstruction`, clear `IRContext::active_regs_` whenever an
   instruction of the predicate is appended. It is then the only reuse left
   for module and global variables, valid between two such instructions,
   with no exception per storage location.

With both parts the PoC passes, `ortest` is 31/31, and the PoC gate drops to
IR-010 and VM-010 only. Part 1 alone (probed on 2026-10-19 and again on
2026-10-26) passes the closure checks but regresses
`ortest/oop/02_inheritance_resolution.orb` and
`poc/ir/ir-013-subclass-without-init.orb`: a module variable bumped by an
`init` reached through two synthesized `init`s reads 1 after the second `new`
instead of 2, because the read after the first `new` lands in `active_regs_`
and the next read reuses that register, which the spiller saves before the
call and reloads after it, old value included. Before part 1 the read was
forwarded from the module's initial store instead, and the spiller's reload
from the global slot made it right by accident. Keeping module and global
variables out of `active_regs_` (as closures already are) was probed too and
also passes; clearing the cache at those instructions is the same rule as
part 1, without a list of storage locations.

Known limit, by inspection: class hooks (`compare`, `equal`, `str`, `repr`,
`hash`) run Orbit code through `EvalSync` inside instructions that are not in
the predicate (comparisons, equality, hashing). A hook that changes a module
variable the calling code then reads is not covered; adding those
instructions would drop the cache at nearly every comparison.

The PoC gained the two module-variable cases (a variable bumped by two calls
and read after each; a variable copied before a call), the `yield` case and
the two synchronization cases.
The old code fails the second module case and the `yield` case; part 1 alone
fails the first module case.

</details>

---

## IR-013 — A subclass without an explicit `init` aborts the compiler
**Severity:** High (compile-time abort on valid source in a debug build; release not tried) · **Status:** FIXED (2026-10-16) · **Location:** `orbit/liftoff/ir/irbuilder.cpp:1065` (`visitFunction`, the synthesized `init` of a subclass)

**Fix verified (PoC `poc/ir/ir-013-subclass-without-init.orb` — prints
`ALL TESTS PASSED`, 6 of 6; `ortest/oop/02_inheritance_resolution.orb` gained
five checks for subclasses without `init`, one and two levels deep, and passes;
`ortest` 24/26 and the PoC gate 47/52, the failures being open findings;
`phi-regalloc.orb` green).** The synthesized `super.init` call now releases
its pushed receiver like every other detached call site:

```cpp
auto *call = (CallInstr *) this->builder_.CreateCallDetached(orbiter::OPCode::CALL, s_init, 1, orbiter::CallMode::METHOD);

this->builder_.AddInstruction(call);

this->builder_.context->stack_push_count -= call->arguments;
```

Without the last line the new `oop/02` checks abort the compile with the same
assert, so the suite guards against a regression. A two-level chain of
synthesized `init`s runs the base `init` once per construction.

<details><summary>Original report</summary>

A class that extends another and declares no `init` gets a synthesized one,
which calls the superclass's:

```cpp
if (this->ct_active_->extends_type && ENUMBITMASK_ISTRUE(node->symbol->flags, SymbolFlags::SYNTHETIC)) {
    ...
    this->builder_.StackPush(self);

    auto *call = this->builder_.CreateCallDetached(orbiter::OPCode::CALL, s_init, 1, orbiter::CallMode::METHOD);

    this->builder_.AddInstruction(call);
}
```

`StackPush` counts the pushed receiver in the context's `stack_push_count`.
`CreateCall` would release it (`builder.cpp:147`), and every call site that
builds its call detached releases it by hand after `AddInstruction`
(`irbuilder.cpp:899`, `:916`, `:1400`). This one does not, so `LeaveContext`
finds the count at 1:
`Assertion failed: (this->context->stack_push_count ==0), function LeaveContext, file builder.cpp, line 709`.

Any subclass without an explicit `init` aborts the compile: `class D : P { }`,
or one with members of its own. A subclass that writes
`pub init() { super.init() }` is fine, which is why no suite noticed: every
subclass in `ortest/` and `issues/poc/` declares its `init`.

**Found** while probing VM-011 with an empty subclass.

**PoC:** [`poc/ir/ir-013-subclass-without-init.orb`](poc/ir/ir-013-subclass-without-init.orb)
*(confirmed live: the assert fires at compile time)*.

**Fix (suggested, verified by probe):** release the pushed receiver after
adding the call, like the other detached call sites:

```cpp
this->builder_.AddInstruction(call);

this->builder_.context->stack_push_count -= 1;
```

With this line the PoC passes 6/6 (the synthesized `init` runs the base `init`,
inherited properties and methods work), and `ortest` and the PoC gate are
otherwise unchanged; the probe was then reverted.

</details>

---

## IR-012 — `self.prop` in a function nested in a method reads through a null owner type
**Severity:** High (SIGSEGV) · **Status:** FIXED (2026-10-20) · **Location:** `orbit/liftoff/ir/irbuilder.cpp:1456` (`visitSelector`, the `self` fast path) → `orbit/orbiter/vm.cpp:1747` (`TARGET_OP(LDOBJP)`, inline path)

**Fix verified (PoC `poc/ir/ir-012-self-prop-in-nested-func.orb` — prints
`ALL TESTS PASSED`, 6 of 6; new suite `ortest/gc/02_function_owner.orb`;
`ortest` 25/27 and the PoC gate 51/56, the failures being open findings;
`phi-regalloc.orb` green).** A function now carries the class its code was
written in, whether it is a method or a function nested in one, and that
class is held through the GC trace rather than by reference count:

- `LDFUNC` copies the running function's `owner_type` into every function it
  creates (`vm.cpp:1588`). Only the class: not the METHOD flag, which decides
  whether a call gets a receiver, so a closure stored in a property and called
  as `obj.cb(x)` still gets none. A closure nested in a closure inherits too.
- `owner_type` takes no reference anywhere: `FunctionNew` no longer increfs it
  for native methods and `FunSharedDel` no longer releases it. A class and its
  methods point at each other, and the hybrid collector treats any object
  with a count above zero as a root, so counted references would keep both
  alive forever. `FunctionTrace` visits `shared->owner_type` instead, so a
  reachable function keeps its class alive.

With the class in place, the inline `self.prop` load in a nested function
reads the right slots, and the code-class users work inside closures as well:
private members (read and written), and `super`.

The first version copied the class but left `FunSharedDel` releasing it:
every nested function freed released a class it never took. Three closures
created in a method and collected were enough for `new A()` to fail with
"'Class' object has no property 'init'" (the count also shares its word with
`kIsInstanceMask`, so going below zero turns a class into an "instance"). The
PoC's last check and `ortest/gc/02_function_owner.orb` guard against it; both
fail on that version.

<details><summary>Original report and notes</summary>

`visitSelector` lowers `self.<member>` to an inline `LDOBJP` (the member's
slot offset, no name lookup) whenever a class is being built:

```cpp
if (obj_base->kind == scanner::TokenType::SELF
    && this->ct_active_ != nullptr
    && ((PhysInstruction *) this->ct_active_->tp_ptr)->opcode == orbiter::OPCode::MKCLZ) {
    const auto *sym = this->sym_t_->LookupMember(key->value);
    if (sym != nullptr && ENUMBITMASK_ISFALSE(sym->flags, SymbolFlags::CONST))
        return this->builder_.LoadObjectProp(base, sym->offset, false, false);
}
```

`ct_active_` is still the class inside a function nested in a method, so the
nested function gets the inline load too. At run time the inline path finds
the instance slots through the **running** function's class:

```cpp
auto *slot = O_SLOT(obj, this_func->shared->owner_type);
```

Only a function attached to the class has an `owner_type`; the nested
function's is null, and the load crashes (SIGSEGV at address `0x72`,
confirmed under lldb). Copying `self` into a local first (`me := self;
... me.v`) takes the generic path and works.

Blocked by SYM-001: `self` is a parameter, so today capturing it crashes
before the property is read. With the SYM-001 fix applied as a probe, a
closure that returns `self` works and one that reads `self.v` crashes here.

**Note (2026-10-16):** SYM-001 is fixed, so this is no longer blocked: a
closure that returns `self` works, and the PoC now crashes at its first
`self.v` in a nested function.

The same inline path is what crashes a decorated method in IR-011, where the
method itself is never given its `owner_type`.

**Note (2026-10-19):** with `LDFUNC` passing the running method's
`owner_type` to the functions it creates, the four checks of the PoC pass. A
check was added for `self.prop` two levels down (a closure inside a closure
inside a method): it still crashes when the inheritance is conditioned on the
creating function being a method, since the middle function is not one.
Conditioning it on the creating function having an `owner_type` covers every
depth.

**Note (2026-10-20):** `LDFUNC` now copies the running function's
`owner_type` into every function it creates (uncommitted). All the PoC's
original checks pass, including two levels down, and `super` and private
members work inside closures. Not closed: the copy takes no reference while
`FunSharedDel` still releases one, so collecting a few closures created in a
method corrupts the class (3 are enough). A check for it was added to the PoC;
what makes it pass is not counting `owner_type` at all (no incref, no decref)
and visiting it in `FunctionTrace`.

**Note (2026-10-16), a constraint on the fix:** whatever IR-011 does to make
a decorator's result callable as a method of the decorated class, the inline
path must not take the slot layout from that class. In
`ortest/decorators/04_decorator_objects.orb` the function installed in
`Handler` was written in `Server` and reads `self.call` on the server: if the
inline load used `Handler` as the layout, it would read the server's slots at
`Handler`'s offsets. The layout has to come from the class the code was written
in (the nested function's enclosing class), not from the class it ends up
installed in.

**PoC:** [`poc/ir/ir-012-self-prop-in-nested-func.orb`](poc/ir/ir-012-self-prop-in-nested-func.orb)
*(confirmed live with the SYM-001 probe applied: the control passes, the
first `self.v` in a nested function crashes)*.

**Fix (suggested, not tried):** take the inline path only when the function
being compiled is the method, i.e. the IR context is the method's own and not
a function nested in it; elsewhere fall back to the named lookup. An
alternative is for the inline path to take the type from the object
(`O_GET_TYPE(obj)`) when the running function has no owner, at the cost of a
branch on the fast path.

</details>

---

## IR-011 — A method whose decorator returns a wrapper cannot be called
**Severity:** High (SIGSEGV; a decorated method is unusable) · **Status:** FIXED (2026-10-27) · **Location:** `orbit/liftoff/ir/irbuilder.cpp:1018` (`visitDecorator`, `MKMETH`), `orbit/orbiter/vm.cpp:1777` (`TARGET_OP(MKMETH)`), `:1541` (`TARGET_OP(LDFUNC)`), `orbit/orbiter/orbcall.cpp:138` (`CheckReceiver`)

**Fix verified (live: PoC `poc/ir/ir-011-decorated-method.orb` prints `ALL
TESTS PASSED`, 3 of 3; the acceptance suites
`ortest/decorators/02_methods.orb` and `04_decorator_objects.orb` pass, as do
the rest of `ortest/decorators/`; `ortest` 31/31, the PoC gate 62/64 with the
open IR-019 and VM-010; `phi-regalloc.orb` green).** A decorated method is
callable like any other method, with the receiver as its wrapper's first
argument, following the decisions below. The fix landed in steps between
2026-10-20 and 2026-10-24, each verified when it landed:

- **The code class is lexical.** A function created by `LDFUNC` inherits the
  `owner_type` of the function that creates it, and a method gets the class
  being built from `LDFUNC`'s third operand (`OPCODE | 4 DST | 4 SRC_CODE |
  4 SRC_DARGS | 4 SRC_CLASS | 8 FLAGS`), before its decorators run. `SETPROP`
  no longer assigns `owner_type`, so it never rewrites a function shared with
  another class. `owner_type` is held by the GC trace, not by the reference
  count (IR-012).
- **A decorator's result is installed by `MKMETH`** (`OPCODE | 4 DST |
  4 SRC_VALUE | 4 SRC_CLASS | 12 RESERVED`), emitted by `visitDecorator` only
  for a method, after the decorators and before the store:
  - anything but an Orbit function (a value, a type, a native function, a
    callable instance) is installed as it is and gets no receiver;
  - a method whose `self` is bound by currying (`Server.decorator2`), or a
    method of this class or of a superclass, is installed as it is;
  - a method of an unrelated class still waiting for its `self` is refused
    when the class is built (`TypeError`, "a decorator of a method of '%s'
    returned a method of '%s'");
  - anything else (a plain function such as `Server.decorator`'s anonymous
    function) is **cloned** with a new `FuncShared`, the same code, closure
    and currying, and promoted: the METHOD flag and `FunctionKind::PROMOTED`.
    A function shared elsewhere is never turned into a method.
- **Receivers are checked in every call mode** (`CheckReceiver`, VM-012). A
  curried method checks its bound `self`; a promoted clone checks nothing,
  since called through an instance its receiver is the object that holds it,
  and the method it forwards to checks its own. A type used as selector is
  dropped unless the callee is a metatype method (VM-011), and `PUSHIF` is
  gone: a selector call always pushes the receiver.
- **Limits, decided:** no decorators in a trait (the parser refuses them),
  and `self` is reserved and cannot name a parameter (a wrapper calls its
  receiver otherwise, e.g. `me`); both pinned by `ortest/decorators/05_traits.orb`
  and `06_self_parameter.orb`.

Configuration passed by name to a curried decorator (`times(n=7)`) is VM-010,
tracked on its own.

<details><summary>Original report</summary>

A decorator on a method is lowered like one on a function: the method is
compiled as an anonymous function, the decorators are called on it, and the
result is stored in the class member with `SETPROP`. When the decorator returns
the method itself, nothing changes and the method works. When it returns a
wrapper, two things go wrong.

**The wrapper is not given the receiver.** It is a plain function, not a
method, and `PUSHIF` pushes the receiver only for a method (VM-008). So
`c.get()` calls a wrapper declared `w(me)` with no arguments; Orbit curries a
call with too few arguments, and `c.get()` returns a `Function` instead of
calling anything. That is the "not callable" symptom. A forwarding wrapper
(`w(...args)`) is called, but forwards no receiver, so the method it calls
curries in turn (observed with the SYM-001 probe applied).

**The method is never attached to its class.** `SETPROP` sets
`shared->owner_type` only on the value it stores, and only when that value is
a method:

```cpp
if (O_IS_OBJECT(value)
    && O_IS_TYPE(value, InstanceType::FUNCTION)
    && ((Function *) value)->shared->IsMethod())
    ((Function *) value)->shared->owner_type = tp;
```

The wrapper is stored, so the decorated method keeps a null `owner_type`. When
the wrapper is given a receiver by hand (`Counter.get(c)`, or `h := Counter.get;
h(c)`) and calls the method, `self.v` takes the inline `LDOBJP` path, which
reads the slots through `owner_type`: SIGSEGV at address `0x72` (confirmed
under lldb). The same wrapper applied by hand to an undecorated method
(`wrap(P.get)(p)`) works, because that method was attached to `P` when the
class was built. A private member reached by name from the method would also be
refused, since the access check uses the same `owner_type` (by inspection,
`vm.cpp:465`).

How a decorated method should receive its receiver is a design choice, to be
discussed before fixing:

- bind any function stored as a class member when it is reached through an
  instance, the way Python binds plain functions;
- mark the decorator's result as a method when the decorated function was one
  (only possible when the result is a fresh `Function`, and it would rewrite
  a function the decorator may share);
- or bind the receiver as the decorator's first argument and leave the wrapper
  unbound.

Whichever is chosen, the decorated method also has to get its `owner_type`
from the class, not from whatever the decorator returns.

**Found** from a report by the user (decorated methods are not callable),
while building `ortest/decorators/02_methods.orb`.

**PoC:** [`poc/ir/ir-011-decorated-method.orb`](poc/ir/ir-011-decorated-method.orb)
*(confirmed live: the control passes, the call through an instance returns a
`Function`, the call with an explicit receiver crashes)*. Its wrappers copy the
decorated function into a local, so they do not depend on SYM-001.

**Note (2026-10-16):** the user fixed two decorator shapes that a framework
needs as acceptance criteria, in `ortest/decorators/04_decorator_objects.orb`:
a method of another object that returns an anonymous function closing over
`self` and `f`, and one that returns a curried private method of its own
(`self.wrapper(f)`). Today the first returns a `Function` from `h.handle()`,
and the second crashes in the decorated method, which has no `owner_type`.

**Note (2026-10-20), first half fixed:** a
method gets its class from `LDFUNC` when it is created, through a third
operand carrying the class being built (`OPCODE | 4 DST | 4 SRC_CODE |
4 SRC_DARGS | 4 SRC_CLASS | 8 FLAGS`), so a decorated method has its
`owner_type` before the decorators run; `SETPROP` no longer assigns it. The
crash with an explicit receiver is gone (the PoC's third check passes, and so
does `Counter.get` taken into a variable in the suites). Still open: the
wrapper is not given the receiver when called through an instance, and a
curried method returned by a decorator (`Server.decorator2`) is refused as a
method of the class it is installed in. The latter briefly seemed to
work while `SETPROP` still set `owner_type`, because it rewrote the shared
`FuncShared` of `Server.wrapper` to `Handler`; removing that is part of the
fix.

**Decisions (2026-10-16, user):**
- A decorator's result installed in a `func` member is callable as a method of
  that class: the receiver arrives as its first argument.
- A plain function returned by a decorator is **cloned** before it is
  promoted (a new `FuncShared`, the same code, closure and currying), so a
  function shared elsewhere is never turned into a method. A method of the
  class or of a superclass is installed as it is; a curried method whose
  receiver is already bound counts as a plain function.
- Access to private and protected members is **lexical**: it follows the class
  the code was written in, not the class a function ends up installed in.
  Functions nested in a method therefore see their class's private members
  (today they do not).
- Consequently a function carries two types that `owner_type` conflates
  today: the class its code belongs to (access checks, `super`, inline slot
  layout) and the receiver type it accepts as a method (`NormalizeMethod`).

Still open: what a decorator may return besides a function (a type, a native
function, a callable instance): refuse it, or wrap it in an adapter.

</details>

---

## IR-010 — Any block nested in a `catch` body aborts the compiler
**Severity:** High (compile-time abort on valid source; release: the `assert` is gone and `Generate` returns an empty handle) · **Status:** FIXED (2026-10-27) · **Location:** `orbit/liftoff/parser/parser.cpp:1238` (`ParseBlock`), `:840` (`ParseTryCatchFinally`)

**Fix verified (PoC `poc/ir/ir-010-block-in-catch.orb` — prints `ALL TESTS
PASSED`, 4 of 4; `ortest` 31/31 and the PoC gate 59/60, the failure being
VM-010; `phi-regalloc.orb` green).** The fix is at the root, in the parser
(the first point of PARSE-019): `ParseBlock` ends the block's location at the
end of its `}`, read before consuming it, and closes the block's scope at the
same offset, so neither covers the token that follows, and a `try` block's
scope stops before the `catch` keyword:

```cpp
block->loc.end = TKCUR_END;
if (!this->MatchEat(TokenType::RIGHT_BRACES, false))
    throw ParserException(19);

if (nested)
    this->sym_t_->LeaveNestedScope(block->loc.end.offset);
```

A block's location now includes its `}`, and everything that closes a scope
at a block's `loc.end` (the catch, a function body) ends at the same offset
as a block scope does.

`EnterNestedScope(catch offset)` then finds the catch scope, and the scopes
nested in the catch body are reachable. Since every pair of adjacent blocks
had the same overlap, the fix covers them all, not the catch alone. In the
same change `ParseTryCatchFinally` sets the end of the whole try statement's
location to the end of its last catch or of its `finally`; it used to keep the
end of the `try` keyword.

Before the fix the PoC aborted the compiler.

<details><summary>Original report</summary>

An `if` or a `for` inside a `catch` body is enough:

```orb
try {
    boom()
} catch e {
    if e.kind == @X { r = 1 }
}
```

```
Assertion failed: (false), function Generate, file irbuilder.cpp, line 2074.
```

It happens in a function and at module scope, with or without a name for the
error. The same blocks inside `try` and inside `finally` compile, and the same
catch body without the nested block compiles too, so the nested scope is the
whole trigger.

**Cause, confirmed under lldb.** `visitBranch` throws `SymbolTableException`
because `EnterNestedScope` cannot find the `if`'s scope (offset 61 in the
reproducer) from the active scope, and the active scope is `[11, 46)`: the
**try** block's, not the catch's. The catch body is entered with
`EnterNestedScope(catch_block->loc.start.offset)`, the offset of the `catch`
keyword (40), and the try block's scope reaches past it: `ParseBlock` closes a
scope with `LeaveNestedScope(TKCUR_END.offset)` after it has consumed the `}`,
so the range ends with the token *after* the block, which for a `try` is the
`catch` keyword itself. That is [PARSE-019](parser.md), which was filed as
Info; this is the first observed consequence. The walk in `EnterNestedScope`
picks the first child that contains the offset, the try scope, and the catch
scope, where the `if`'s scope lives, is never entered. Symbols declared in the
catch body still resolve, because lookups fall back to the parent; only a
nested scope exposes it.

**Found** while writing the `defer` loop of the VM-004 PoC: a `catch` that
counted refusals with an `if` aborted the compile. That PoC keeps its catch
body flat.

**PoC:** [`poc/ir/ir-010-block-in-catch.orb`](poc/ir/ir-010-block-in-catch.orb)
*(confirmed live: aborts the compiler; the same file with the nested blocks
flattened prints `ALL TESTS PASSED`)*. It is expected to fail until this is
fixed, per GUIDE §11.

**Fix (suggested, not tried):** end a block's scope at the `}` it consumed
rather than at the token after it (PARSE-019), which keeps sibling scopes from
overlapping. Fixing only the catch, by entering its scope through something
other than an offset that a neighbour may cover, would leave the overlap in
place for every other pair of adjacent blocks.

</details>

---

## IR-009 — `BasicBlock::AddInstructionFirst` dereferenced a null head on an empty block
**Severity:** High (compile-time crash on valid source; release: null write) · **Status:** FIXED (2026-10-05) · **Location:** `orbit/liftoff/ir/basicblock.h:149` (`AddInstructionFirst`)

**Fix verified (regression PoC `poc/ir/ir-009-addfirst-empty-block.orb` — prints
`COMPILED`; mutation-tested by restoring the old line, which segfaults the new
`ortest/exceptions/01_abrupt_exit.orb`).** The helper now handles the empty
block the way its two siblings already did, and sets `instr.tail` when the
block had none:

```cpp
instr->next = this->instr.head;
instr->prev = nullptr;

if (this->instr.head != nullptr)
    this->instr.head->prev = instr;
else
    this->instr.tail = instr;

this->instr.head = instr;
```

<details><summary>Original report</summary>

```cpp
void AddInstructionFirst(Instruction *instr) noexcept {
    instr->basic_block = this;

    instr->next = this->instr.head;
    this->instr.head->prev = instr;   // <- null when the block is empty

    this->instr.head = instr;
    ...
}
```

`AllocStackSlots` calls it to place an `ALLOCA` at the top of the function's
entry block, and that block is still empty when the function's first statement
is a construct that needs stack slots. A `try` needs them for its
`ExceptionContext`, so nine lines are enough:

```orb
func m() {
    loop {
        try { } finally { }
        break
    }
    return 1
}
```

```
EXC_BAD_ACCESS (code=1, address=0x48)
BasicBlock::AddInstructionFirst(...) at basicblock.h:153
```

No jump, no panic and no abrupt exit are involved: the same source with a
single declaration before the loop compiles and runs, which is why this stayed
hidden. In a release build the assert-free path writes through the null pointer.

**Sibling of [IR-002](#ir-002--trap-new-x-asserts-in-addinstructionbefore-compile-time-crash).**
Commit `de39847` ("fix(ir): handle edge cases for instruction insertion in
`BasicBlock`", 2026-07-13) added the `tail` update to `AddInstructionAfter` and
replaced `AddInstructionBefore`'s `assert(instr->prev != nullptr)` with a proper
`head` update. It left the third insertion helper untouched, and
`basicblock.h` was not modified again until this fix.

**Found** while validating `break`/`continue` out of a `try` (VM-003): the
reproducer that exposed it contains no jump at all, so it is unrelated to that
work beyond having been reached through it.

</details>

---

## IR-008 — `continue` out of a `try` block asserts at compile time
**Severity:** Medium (compile-time abort on valid source; release: returns nullptr into codegen) · **Status:** FIXED (2026-10-05) · **Location:** `orbit/liftoff/ir/jblock.cpp:94` (`FindLabeledBlock`)

**Fix verified (`ortest/exceptions/01_abrupt_exit.orb` and
`poc/vm/break-out-of-try.orb`, both green).** Same one-line cause and same fix
as [VM-003](vm.md): `FindLabeledBlock` resolved an unlabelled target to the
innermost jump block that was not `SYNC`, which for a `continue` inside a `try`
is the TCF block, and `GetJBlockBegin` accepts only `LOOP`, `FOR_IN` and
`LABEL`. The unlabelled case now returns only a `LOOP` or a `FOR_IN`, so the
block handed to `GetJBlockBegin` always has a beginning. This finding and
VM-003 were the two halves of one defect, seen through a rejecting assert and
through a tolerant lookup.

<details><summary>Original report</summary>

```orb
for var i in [1, 2, 3, 4, 5] {
    try {
        if i == 3 { continue }
    } catch e {
    }
}
```

```
Assertion failed: (false), function GetJBlockBegin, file jblock.cpp, line 72.
```

`irbuilder` walks the jblock chain looking for the loop to continue, and when it
crosses a `TCF` block it calls `GetJBlockBegin(b_target)`. That function only
answers for `LOOP`, `FOR_IN` and `LABEL`, and asserts on everything else; the
block it is handed here is not one of the three. In a release build the assert
is gone and the `nullptr` it returns flows into the emission of the `TSPA`
operand.

The `break` spelling of the same construct compiles and is wrong at runtime
instead: see [VM-003](vm.md). **Not a regression** from the concurrent
`ExceptionContext` work: verified against both revisions, identical.

**PoC:** none filed; it aborts rather than producing output, and the six lines
above reproduce it directly *(confirmed live)*.

**Fix:** resolve the continue target from the loop block rather than from the
`TCF` block that interrupts the walk, so `GetJBlockBegin` is asked about a block
that has a beginning. Fix alongside VM-003, since the two are the two halves of
"leave a loop from inside a try".

</details>

---

## IR-007 — A blank (`_`) used as a value asserts instead of reporting an error
**Severity:** Medium (debug: abort; release: null dereference) · **Status:** OPEN · **Location:** `orbit/liftoff/ir/irbuilder.cpp` (`visitIdentifier`)

`_` is a discard: it names no storage, so it can only ever appear as the target
of a declaration or an assignment. Reading it is meaningless, and the parser
accepts it anyway, because `ParseIdentifier` builds a BLANK identifier without
knowing whether the caller wants a target or a value. The IR builder is where
that is caught, and today it catches it with an assert:

```cpp
Instruction *IRBuilder::visitIdentifier(const parser::Identifier *node) {
    if (node->kind != scanner::TokenType::IDENTIFIER) {
        assert(false); // TODO: exception here
    }
```

In a debug build the program aborts with no diagnostic beyond the assert text.
In a release build `NDEBUG` removes the assert, execution falls through, and
the `node->symbol` that follows is the `nullptr` the parser stored for a blank.

```orb
io.print(_)      // Assertion failed: (false), visitIdentifier
y := _           // same
_ + 1            // same
```

**Blocked by [COMP-001](compiler.md).** This is not a defect in the check, which
is in the right place and detects the right thing: the compiler has no way to
raise a diagnostic at all, which is what COMP-001 records. The assert is a
deliberate placeholder for the exception that belongs there. When the compiler
gains error propagation, this becomes a message along the lines of *"`_`
discards a value and cannot be read"*, with the offending location, and closes.

**PoC:** none filed; a PoC would assert-abort rather than produce output, and
the three lines above reproduce it directly *(confirmed live)*.

**Fix:** replace the assert with the compiler's error path once COMP-001 lands,
reporting the identifier's own `loc`. Note that the condition should name the
case it rejects (`node->kind == scanner::TokenType::BLANK`) rather than
excluding everything that is not `IDENTIFIER`: `ParseIdentifier` also sets
`SELF` and `SUPER` on nodes that reach this visitor legitimately.

---

## IR-006 — The call-protocol registers (R10/R11/R12) are pinned on the value's producer, so two such values collide
**Severity:** High (silent miscompile + interpreter crash) · **Status:** FIXED (2026-07-24) · **Location:** `SetNargs`/`SetRest`/`SetKwargs` (`orbit/liftoff/ir/instruction.h:170-186`), fixed in `IRBuilder::CreateCall` (`orbit/liftoff/ir/irbuilder.cpp`)

Named, rest and keyword arguments reach the callee through fixed registers —
`kCallNArgsReg = 10`, `kCallRestReg = 11`, `kCallKWArgsReg = 12` — read back by
`ArgumentBinder::Bind` from `regs.r10/r11/r12`.

`SetRest` (and its siblings) applied the pin to the **producing** instruction:

```cpp
void SetRest(Instruction *instr) const {
    instr->assigned_reg = kCallRestReg;   // the producer is homed in R11 for its whole life
    this->SetOperand(2, instr);
}
```

so the value stayed homed in R11 for its entire live range rather than just
around the call. With two spread calls whose operands are live at the same time,
**both producers were homed to R11** — two overlapping intervals sharing one
register, the exact invariant violation of [IR-003](#ir-003--two-call-results-live-simultaneously-collide-in-the-return-register-r13),
only on R11 instead of RR.

`AllocateSpecificRegister` does handle pins on pool registers (it spills both and
keeps the longer-lived as owner-of-record, "*from interval.start onward the
register is shared scratch*"), but that only works when the uses are separated.
Two long, interleaved live ranges sharing a home register cannot both be correct:
the reloads overwrite each other.

```orb
func f(a, ...rest) { return tostr(a) + "|" + tostr(rest) }

x := [1, 2, 3]
y := [7, 8, 9]

f(x...)   # ok
f(y...)   # assertion, or silently receives the previous call's rest list
```

Two symptoms, depending on what the clobbered register held:

- **crash** — a container under construction lived in R11, the call wrote the
  rest list over it, and `ADDELEM` asserted on a non-object (`vm.cpp`, `assert(O_IS_OBJECT(obj))`);
- **silent miscompile** — a later call read a stale R11 and received the
  *previous* call's rest list (`h(args...)` returned `7,99|[8]` instead of `1,99|[2, 3, 4, 5]`).

Confirmed in the IR dump: both list literals were assigned `REG: 11`.

**Fix.** Keep the pin, but shorten its live range: copy the value into the
protocol register immediately before the call instead of homing the producer
there. In `IRBuilder::CreateCall` the call is still *detached* at that point
(`CreateCallDetached` uses `CreateObject`, which does not attach) while
`CreateMove` attaches immediately, so the moves land right in front of the call:

```cpp
if (nargs != nullptr)  call->SetNargs(this->builder_.CreateMove(nargs));
if (rest != nullptr)   call->SetRest(this->builder_.CreateMove(rest));
if (kwargs != nullptr) call->SetKwargs(this->builder_.CreateMove(kwargs));
```

The pinned interval now spans only `MOV -> CALL` and cannot overlap another
call's. R10/R11/R12 stay general-purpose in the allocator's pool, and neither
LinearScan, the codegen (which only ever emitted `operands[0]` for a call) nor
the VM needed changing.

```
before:  NLIST REG:11 · NLIST REG:11 · ... · CALL
after:   NLIST REG:0  · NLIST REG:2  · ... · MOV REG:11 · CALL · MOV REG:11 · CALL
```

**PoC:** [`poc/ir/ir-006-spread-reg-collision.orb`](poc/ir/ir-006-spread-reg-collision.orb)
`(fixed)` — 12 checks; verified red (assertion) before the fix and green after.

**Note.** Found while testing the rest-argument paths of `ArgumentBinder`; no
suite covered argument passing at the time, so the gate was green with this bug
live. Call-mechanics coverage now lives in `ortest/calls/`.

---

## IR-005 — Instantiating a class three levels deep in an inheritance chain hangs
**Severity:** High (interpreter hang) · **Status:** FIXED (2026-07-22) · **Location:** `LoadFromObjectProp` SUPER branch (`orbit/orbiter/vm.cpp:779`)

**Root cause (confirmed).** `super.m()` resolved the lookup base from the
*receiver's runtime type* (`GetTypeInfoFromObject(self)` — always the most-derived
class) rather than from the class that lexically defines the running method. As
the constructor chain runs, `self`'s type never changes, so `super.init()` inside
an inherited `init` re-resolved to that same `init`: `B.init` reached during
`new C()` computed `super` off `C` and got `B` again → unbounded recursion, which
grew the VMStack without limit (the sample showed the thread permanently inside
`VMStack::Grow`/`MemoryCopy`, not a C++ loop). Two levels worked only by
coincidence — the top method's owner equals the receiver type.

**Fix.** In the SUPER branch, walk from the enclosing class instead:
`type = O_GET_TYPE(func->shared->owner_type)`. The earlier `TIFindProperty`
cycle-guard hypothesis was wrong (the chain terminates at nullptr). Regression
guard: `ortest/oop/02_inheritance_resolution.orb` now exercises 3- and 4-level
chains (fields + `super.who()` method chain).

`A <- B` constructs fine; `A <- B <- C` makes `new C()` spin forever. The hang is
at **runtime** — compilation completes and statements before the construction run
and print normally.

```orb
class A  { pub init() { } }
class B : A { pub init() { super.init() } }
class C : B { pub init() { super.init() } }

new B()   # ok
new C()   # hangs
```

**Pre-existing**, and independent of IR-004: reproduces identically with the
`LoadFromObjectProp` fix reverted (verified by stashing it and rebuilding).

**Hypothesis (unverified).** `TIFindProperty` (`oobject.cpp:262`) walks the chain
with `type = O_GET_TYPE(type)` and has **no cycle guard**. If the metatype root is
self-referential (`Type`'s type being `Type`, the usual arrangement), then any
lookup that misses all the way up loops forever. A two-level chain may always find
its target before reaching the root, while a three-level one does not. Worth
checking that root link first — a cycle guard there would be a cheap fix if so.

**PoC:** [`poc/ir/ir-005-three-level-chain-hang.orb`](poc/ir/ir-005-three-level-chain-hang.orb)
`(fixed)` — now terminates and passes; kept in the poc gate as a regression guard.

**Test suite:** `ortest/oop/02_inheritance_resolution.orb` now exercises 3- and
4-level chains (previously capped at two while this was live).

<details><summary>Original report (2026-07-15, OPEN)</summary>

Every `CALL` result is pinned to the return register R13 and *keeps* R13 as its
`assigned_reg`. When two call results are bound to locals and stay live together,
the allocator ends up with **two overlapping intervals both assigned R13** — a
violation of the core invariant that overlapping intervals never share a
register. Both are spilled across the intervening call and, at their shared use,
both reload into R13 back-to-back; the second reload overwrites the first.

Post-linearscan bytecode of `two_call` (`a := id(7); b := id(9); return a + b`):

```
CALL  R13        # a
...
CALL  R13        # b
SKLDR R13        # reload a into R13
SKLDR R13        # reload b into R13   <-- overwrites a
ADD   R0         # R0 = R13 + R13 = b + b
```

So `a + b` evaluates to `b + b` (7+9 → 18). The reload strategy
(`load->assigned_reg = instruction->assigned_reg`) faithfully reloads each value
into its own home register, which is correct *unless* two live values were given
the same home — which R13 pinning does.

**Scope.** Only triggers when ≥2 call results are **bound and kept live** into a
later op. Results consumed immediately (pushed as call args, folded once) are
fine, because they never co-reside in R13 — so `add(add(1,2), add(3,4))` and
`sq(add(2,3))` are correct. `three_call` (`a*100 + b*10 + c`) happens to pass
because its arithmetic forces the results out of R13. Present at HEAD,
independent of the register-leak fix.

**PoC:** [`poc/ir/ir-003-r13-crosscall.orb`](poc/ir/ir-003-r13-crosscall.orb)
`(confirmed live)` — RED until fixed. Broader tiered coverage lives in
`ortest/regalloc/*.orb` (suite 04 isolates this; 01–03, 05 stay green), run via
`ortest/run.sh`.

**Fix (suggested).** Model the call result as an ordinary SSA value instead of
leaving it pinned to R13. The cleanest form is a dedicated pre-pass after
`ComputeLiveIntervals` — find calls, materialise the spill store/reload of every
interval that spans them as real IR, re-run `ComputeLiveIntervals`, then
allocate — which makes every reloaded value first-class and dissolves the R13
special case (it also removes the fragile mutate-IR-during-allocation coupling).
Since the VM clobbers all registers on a call (no callee-saved), spilling every
spanning value is optimal, not wasteful. *(This is the path that was taken.)*

</details>

---

## IR-004 — A derived class resolves its own members through its superclass (wrong `init`, shadowed properties)
**Severity:** High (silent wrong behaviour) · **Status:** FIXED (2026-07-17) · **Location:** `orbit/orbiter/vm.cpp` (`LoadFromObjectProp`)

**Fix verified (`poc/ir/ir-004-derived-ctor-body.orb` GREEN — full tracker suite
21/21, `ortest/run.sh` 7/7).** Root cause found by the project author: the
`type_` field is **overloaded**. For a builtin type object it points at the
*metatype* (`O_GET_TYPE(String)` → `Type`), but for a user class it points at the
*superclass* (`O_GET_TYPE(Derived)` → `Base`). `LoadFromObjectProp` resolved a
type object through `O_GET_TYPE(obj)` first:

```cpp
const bool obj_is_type = O_IS_OBJECT(obj) && !O_GET_RC(obj).IsInstance();
const auto *type = obj_is_type ? O_GET_TYPE(obj) : GetTypeInfoFromObject(...);
```

so `new Derived()` looked up `init` starting at `Base` and got **Base's**
constructor. The derived constructor never ran at all — which is why its field
defaults, its explicit assignments and every statement after `super.init(...)`
appeared to be dead code.

The metatype-first order exists for a reason and was kept: for a builtin like
`String` it prevents an *instance* method being picked up and applied to the
non-instance type object. The fix only special-cases classes, where `type_` is an
inheritance link rather than a metatype link: for a `CLASS` type object (outside a
`SUPER` lookup) the class's **own** chain is searched first. `TIFindProperty`
already walks up to the super and on to the metatype, so inherited members still
resolve normally.

Not limited to constructors: before the fix, *any* member read off a derived
class type object returned the super's version — `D.who == B.who` evaluated to
`true` (same Function object) with `who` overridden in `D`; it is `false` now.

<details><summary>Original report (2026-07-17, OPEN)</summary>

The parser *requires* `super.init(...)` to be the first statement of a derived
class constructor (`parser.h:95`). Everything after it is then silently dead: the
constructor body simply does not run past that point. No error, no diagnostic.

Consequences, all silent:

- a derived class's own field defaults (`pub var b = 20`) stay `nil`;
- explicit assignments in the constructor (`self.c = 30`) are lost;
- any other constructor logic (side effects, validation) is skipped.

```orb
class Base    { pub var a = 10   pub init() { } }
class Derived : Base {
    pub var b = 20
    pub init() { super.init()   self.b = 99 }   # neither the default nor the assignment lands
}
new Derived().b   # -> nil (expected 99)
```

Storage and slot resolution are **not** at fault: the identical write from an
ordinary method works (`p.setd()` → `p.d == 7` in the PoC), and the inherited
field set by the super's constructor is correct.

`EXECSUB` (`vm.cpp:1540`) looks like a proper call: it `PushState()`s, switches
the context to the super's `Code`, and `goto BEGIN`; the super's `RET` →
`PopState()` restores the whole `FiberContext` (code object included) plus IP/BP,
so control *should* resume right after the `EXECSUB`. That points at the IR
builder not emitting the trailing statements of a derived constructor at all,
rather than at the VM — worth confirming with an IR/bytecode dump of the derived
`init` before fixing.

**PoC:** [`poc/ir/ir-004-derived-ctor-body.orb`](poc/ir/ir-004-derived-ctor-body.orb)
`(confirmed live)` — 3 checks RED, 2 control checks green.

**Fix.** Determine whether the derived-constructor lowering emits the statements
following the `super.init(...)` call; if it does not, emit them after the
`EXECSUB` and keep the constructor's own `RET` as the terminator. If it does emit
them, the return path from `EXECSUB` is not resuming the caller's code object as
`PopState` suggests, and the VM side needs the fix instead.
*(2026-07-17: both hypotheses were wrong — the lowering and `EXECSUB`/`PopState`
are fine. The derived constructor was never invoked in the first place: the
`init` lookup resolved to the superclass. See the update above.)*

</details>

---

## IR-003 — Two call results live simultaneously collide in the return register R13
**Severity:** High (silent miscompile) · **Status:** FIXED (2026-07-17) · **Location:** `orbit/liftoff/ir/linearscan.cpp`, `orbit/liftoff/ir/intervalspiller.{h,cpp}`, `orbit/liftoff/ir/ircontext.cpp` (`CallerSaveSpiller`)

**Fix verified (`poc/ir/ir-003-r13-crosscall.orb` GREEN — full tracker suite
20/20; tiered battery `ortest/regalloc/01..05` all green, including suite 04
which isolates this bug: `two_call`=16, `mul_call`=12, `sum_call`=32,
`three_call`=123).** Resolved by restructuring the allocation pipeline
(essentially the pre-pass path suggested in the original report):

- **`IRContext::CallerSaveSpiller`** — new pre-pass between the two liveness
  computations (`ComputeLiveIntervals` → spill pass → `ComputeLiveIntervals` →
  `LinearScan`, see `compiler.cpp`). It collects every call site
  (CALL/EXECSUB/NTCALL, caller-clobbered in Orbit's VM) and, for each interval
  spanning one, materialises the stack save/reload as real IR via
  `IntervalSpiller::Spill(interval, call_offset)`. After the re-run of liveness,
  no interval spans a call, and each reload is a **fresh, unpinned SSA value**
  the allocator assigns normally — two call results never co-reside in R13.
- **`IntervalSpiller`** — extracted spill mechanism (slot allocation with
  expiry-driven reuse, SKSTR/SKLDR emission, STGOFF store-to-load forwarding,
  operand rewrite). Ctor flag `inherit_reg`: `false` in the pre-pass (reloads
  left unassigned so they get their own intervals/registers — the crux of this
  fix), `true` inside `LinearScan` (no liveness re-run there, reloads must
  inherit the home register).
- **`LinearScan`** hardening found during review: on register contention
  (`SpillAndAssignRegister`, and the true-overlap path of
  `AllocateSpecificRegister`) **both** sharers are spilled from
  `interval.start` — the incoming def writes the register regardless, so a
  one-sided spill silently clobbers the "stable" value — and the longer-lived
  of the two stays in `active_` as the register's owner-of-record (freeing at
  the shorter end would let a future interval claim the register while the
  survivor's reloads still write it).

A residual limit initially noted here — R13 sat in `free_registers_` (the pool
was built from `kGeneralPurposeRegistersCount` = 14), so under full pressure an
ordinary value could receive it and later collide with a pinned call result on
one instruction — was **also closed (2026-07-17)**: the allocator is now built
with `kAllocatableRegistersCount` = 13 (`vm.h`; R0–R12, RR excluded — the
runtime constant stays 14 because GC root scanning and the generator
`regs_dump` size the register file with it). Companion changes in `LinearScan`:
out-of-pool pinned registers are claimed directly by `AllocateSpecificRegister`
(conflict scan first) instead of hitting the logic-error throw; an expiring
pinned interval no longer leaks RR into the pool (`ExpireOldIntervals`); the
"all registers busy" test reads `free_registers_.empty()` rather than
`active_.size() == total_regs_` (active_ also tracks pinned intervals); and
`SpillAndAssignRegister` picks its victim only among pool-register holders.
Verified: full battery green plus a targeted stress (pinned RR result held live
across 16 temporaries exhausting the pool, and two call results with 14 live
temporaries in between).

---

## IR-002 — `trap new X()` asserts in `AddInstructionBefore` (compile-time crash)
**Severity:** High (silent wrong codegen — panics swallowed) · **Status:** FIXED (2026-07-13) · **Location:** `orbit/liftoff/ir/basicblock.h:126` (`AddInstructionBefore`), `basicblock.h:101` (`AddInstructionAfter`)

**Fix verified (`poc/ir/ir-002-trap-new.orb` — full suite 19/19 green,
including `phi-regalloc`).** `AddInstructionBefore` now updates the block's
list head when inserting before the head instruction, and
`AddInstructionAfter` symmetrically updates the tail:

```cpp
if (instr->prev != nullptr)
    instr->prev->next = before;
else
    this->instr.head = before;   // instr was the head
```

Root cause (confirmed with lldb; the original hypothesis was wrong): NOT the
trap guard insertion. LinearScan spills the `NOBJ` result of `new`, and
`SpillToStackAndReloadUses` (`linearscan.cpp:256`) inserts the `SKLDR` reload
before its use `STRES` — the store-result of `trap`, which is the **first
instruction of the trap's finally block** (`prev == nullptr` → assert).

An intermediate fix that merely dropped the assert (guarding the
`instr->prev->next` write) traded the crash for a **silent miscompile**,
verified live: the reload stayed linked in the `prev` chain but was invisible
to every forward walk from `instr.head` (codegen, LinearScan, `SlotIndexes`),
while `size += 4` still counted it — so all block offsets from
`CalculateCodeSize` (`codegen.cpp:483`) drifted +4 past the emitted layout,
the trap handler offset landed one instruction past `STRES`, and on the panic
path the panic was swallowed (`trap` yielded a stale non-Result value).
Updating `instr.head` fixes both modes at once; the PoC covers success-path
value integrity and panic-path catching under register pressure.

The `AddInstructionAfter` tail update fixes the symmetric latent flaw
(insert-after-tail left `instr.tail` stale; a later `AddInstruction` append
would relink through the stale tail and silently drop the inserted
instruction — reachable in principle via `AllocStackSlots`'
insert-after-`last_alloca`, `builder.cpp:108`). Never reproduced; fixed
preventively for the invariant.

Note for PoC writers, found while verifying: a bare property access used as a
statement (`q.boom`) is a use-less pure producer and gets removed by DCE — it
never panics at runtime. Use a call (`q.boom()`) to make a panic real.

<details><summary>Original report (2026-07-06, OPEN)</summary>

**Reproducer (minimal, 1 line — crashes at compile time, no execution needed):**

```orb
trap new A()
```

Any variant crashes identically: `err := trap new Error(@x, "y")`,
parenthesized `trap (new ...)`, inside a function body, with any class.
`trap` on a plain call (`trap f()`) is fine; `new` without `trap` is fine.
The combination is what breaks: first observed via
`err := trap new Pattern("(unclosed")` in a regex test.

```
Assertion failed: (instr->prev != nullptr), function AddInstructionBefore,
file basicblock.h, line 131.
```

**Hypothesis:** the trap lowering inserts its guard instruction *before* the
first instruction of the trapped expression; the `new` lowering starts a
sequence whose first instruction is at the head of its basic block
(`prev == nullptr`), a case `AddInstructionBefore` does not handle.
*(2026-07-13: hypothesis disproved — right method, wrong caller; see update.)*

</details>

---

## IR-001 — Phi targets bypass live-range conflict detection in LinearScan
**Severity:** High (silent miscompile) · **Status:** FIXED (imported 2026-06-15; fix predates the tracker) · **Location:** `orbit/liftoff/ir/linearscan.cpp` (`LinearScan::Allocate`), `instruction.h` (`PhiInstr::AddTarget`/`SetRegister`), `irbuilder.cpp` (join sites)

**Fix verified (regression suite `poc/ir/phi-regalloc.orb` — prints `ALL TESTS
PASSED`).** Resolved via a variant of fix #1 (edge copies emitted at IR-build
time):

- A `MOV` opcode (register-to-register copy, unary format) was added to the VM.
- At every control-flow join (`visitTernary`, `BinaryAndOr`,
  `CreateJumpForElvisOrNil`, `visitNilSafety`) each Phi target is now wrapped in
  a `Builder::CreateMove` placed at the end of its branch. The MOV is a fresh,
  single-use value consumed only by the Phi, so the register shared by the Phi
  and its targets is written immediately before the join — nothing can be
  allocated inside the previously-invisible window on a path where the value
  matters. The branch values get ordinary, fully conflict-checked intervals.

This also fixed two latent issues:
- a `CALL` used directly as a Phi target had its pinned return register
  overwritten, which CALL's encoding (no DST field) could not honour;
- a Phi whose value is discarded (e.g. `a || b` as an expression statement) was
  never allocated, leaking `kDoNotAllocateReg` into the emitted DST bits.
  `ComputeLiveIntervals` now gives a use-less Phi a point interval.

`Builder::LoadImmediate`'s accumulation chain intentionally keeps the direct
`AddTarget` (a same-register constraint over back-to-back LDIMM chunks, not a
join) — see the invariant on `PhiInstr::AddTarget`.

<details><summary>Original report (from KNOWN_ISSUES.md)</summary>

**Symptoms**

When a function contains a ternary expression (or an `AND`/`OR` short-circuit)
whose two branches contain instructions that allocate registers, one of those
intermediate instructions may end up assigned to the **same** physical register
as the Phi targets. The result is that a value the codegen believes lives in
register R is silently overwritten by a sibling instruction also assigned R,
because the allocator never recorded a conflict.

Concretely observed: emitting `LDIMM 0` (slice start of `retval[:nbytes]`) inside
the `else` branch of a ternary clobbers the register that, after Phi propagation,
is supposed to hold the value coming out of the `then` branch of the same ternary.

**Root cause**

`PhiInstr::AddTarget(src)` marks each target with
`src->assigned_reg = kDoNotAllocateReg`:

```cpp
PhiInstr *AddTarget(Instruction *src) noexcept {
    assert(this->index<2);
    src->assigned_reg = kDoNotAllocateReg;   // <-- target made invisible to allocator
    this->SetOperand(this->index++, src);
    return this;
}
```

`LinearScan::Allocate` then **skips** any interval whose instruction has
`assigned_reg == kDoNotAllocateReg`:

```cpp
for (auto &interval : intervals) {
    if (interval.instr->assigned_reg == kDoNotAllocateReg)
        continue;
    ...
}
```

This means the target's live interval — `[target_offset, phi_offset]` — is never
represented in `active_`, so the register it will eventually receive from the
Phi is **not reserved** during that window.

When the Phi itself is finally processed at `phi_offset`, `SetRegister` is called
and propagates the chosen register back to every target:

```cpp
void SetRegister(const I16 reg) noexcept override {
    this->assigned_reg = reg;
    for (auto i = 0; i < this->index; i++)
        ((Instruction *) this->operands[i].value)->SetRegister(reg);
}
```

But by then, any interval whose lifetime fell strictly between `target_offset`
and `phi_offset` may have been allocated the **same** register, because at
allocation time it looked free.

In the observed case the offending intermediate is an `LDIMM 0` emitted by
`visitSubscript` (line 1319) for the implicit slice-start; any short-lived
instruction inside the same Phi-target window can trigger it.

**Why it surfaced**

A fix in `visitAssignment`/`visitUpdate` (removal of `RFindFirstInstruction`)
made the IR more correct: the LHS `LDOBJP` is converted to `STOBJP` while RHS
loads survive. Before the fix the RHS loads were partially mutilated, which
incidentally kept register pressure lower. With the corrected IR there are more
concurrent intervals, and one now reliably falls into the unguarded window
between a Phi target and the Phi itself.

**Possible fixes** (in order of invasiveness)

1. **(Simplest, correct)** Stop marking Phi targets as `kDoNotAllocateReg`. Let
   them be allocated normally and emit an explicit move from the target's
   register to the Phi's register at the end of each incoming edge. Textbook,
   always correct; costs one MOV per branch when coalescing fails.
2. **(Mid)** Replace `kDoNotAllocateReg` with a "register hint": allocate Phi
   targets as ordinary intervals but ask the allocator to try the same register
   for all targets and the Phi. On success no MOV; on failure fall back to #1.
3. **(Invasive)** Pre-allocate Phi registers up front via
   `AllocateSpecificRegister` per target before the linear sweep, using eviction
   to resolve conflicts — at the cost of less optimal register choice.

**Recommended path:** start with fix #1 (drop the mark, emit a MOV at the join);
layer fix #2 later as an optimization once move-coalescing exists. *(This is the
path that was taken.)*

</details>
