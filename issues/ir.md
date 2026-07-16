# ir — bug report

**Component:** `orbit/liftoff/ir` (instruction.h, linearscan.cpp, irbuilder.cpp) · **ID prefix:** `IR`
**PoCs:** [`poc/ir/`](poc/ir/) · **Last reviewed:** 2026-07-15
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

> Imported 2026-06-15 from `orbit/liftoff/ir/KNOWN_ISSUES.md` (ISSUE-001 → IR-001).

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

## IR-003 — Two call results live simultaneously collide in the return register R13
**Severity:** High (silent miscompile) · **Status:** OPEN · **Location:** `orbit/liftoff/ir/linearscan.cpp` (`AllocateSpecificRegister`, `SpillToStackAndReloadUses`), reload register choice at `linearscan.cpp:254` (`load->assigned_reg = instruction->assigned_reg`)

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
independent of the IR-004/leak fixes.

**PoC:** [`poc/ir/ir-003-r13-crosscall.orb`](poc/ir/ir-003-r13-crosscall.orb)
`(confirmed live)` — RED until fixed. Broader tiered coverage lives in
`ortest/regalloc_*.orb` (suite 04 isolates this; 01–03, 05 stay green), run via
`ortest/run.sh`.

**Fix.** Model the call result as an ordinary SSA value instead of leaving it
pinned to R13: right after the call, move it out of R13 into a normally-allocated
register (a `MOV V, R13`), so two results never share R13. The cleanest form is a
dedicated pre-pass after `ComputeLiveIntervals` — find calls, materialise the
spill store/reload of every interval that spans them as real IR, re-run
`ComputeLiveIntervals`, then allocate — which makes every reloaded value
first-class and dissolves the R13 special case (it also removes the fragile
mutate-IR-during-allocation coupling). Since the VM clobbers all registers on a
call (no callee-saved), spilling every spanning value is optimal, not wasteful.

---
