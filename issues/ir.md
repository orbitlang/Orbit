# ir — bug report

**Component:** `orbit/liftoff/ir` (instruction.h, linearscan.cpp, irbuilder.cpp) · **ID prefix:** `IR`
**PoCs:** [`poc/ir/`](poc/ir/) · **Last reviewed:** 2026-06-15
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
