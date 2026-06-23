# `issues/` — Issue tracking standard & workflow

This document defines how project issues (bugs, defects, known issues) are
recorded, updated, and managed under `issues/`. It is the single source of truth
for the conventions; follow it exactly so the reports stay consistent and
machine-mergeable, whether written by one person or by many agents in parallel.

> TL;DR: one Markdown file per component, one stable ID per finding, a status
> field that only ever moves forward, a collapsible archive of the original
> text when a finding is closed, and a `README.md` index that mirrors reality.

---

## 1. Directory layout

```
issues/
  README.md     # index: component table, priorities, review log (shared state)
  GUIDE.md      # this file
  <component>.md  # one report file per audited component
```

- **One file per component.** A "component" is a coherent unit — a source file,
  a small cluster of closely related files, or a subsystem. Examples in use:
  `scanner.md`, `ibuffer.md`, `sbuffer.md`, `parser.md`,
  `utf8-stringbuilder.md`.
- File names are lowercase, `kebab-case`, ending in `.md`. Pick a name that
  matches the code unit, not the bug (`scanner.md`, not `scanner-bugs.md`).
- Do **not** create per-finding files. Findings live as sections inside their
  component file.

---

## 2. Finding identifiers

Every finding gets a **stable ID**: `PREFIX-NNN`.

- **PREFIX** — a short uppercase tag derived from the component, unique per
  file. Established prefixes: `SCAN` (scanner), `IBUF` (input buffer),
  `SBUF` (store buffer), `PARSE` (parser), `UTF8` (UTF-8 codec), `IR`
  (liftoff/ir — register allocator / IR builder). When you start a new
  component, choose a new prefix and record it in the README table.
- **NNN** — zero-padded sequential number within that prefix, starting at `001`.
- IDs are **append-only and immutable**: never renumber, never reuse a number
  (even after a finding is closed or deleted), never change a finding's prefix.
  Cross-references elsewhere (commits, other reports, memory) must keep working.
- IDs are **local to their component file**, so `SCAN-001` and `PARSE-001`
  coexist. This also means ID allocation never needs global coordination — see
  §8.

---

## 3. Anatomy of a finding

Each finding is a `##` section. Template for an **open** finding:

```markdown
## PREFIX-NNN — Short imperative title of the problem
**Severity:** <level> · **Status:** OPEN · **Location:** `path/to/file.ext:line`

One or two paragraphs describing the bug: what the code does, why it is wrong,
and the consequence. Quote the offending code if it clarifies.

```c
// minimal offending snippet (optional but encouraged)
```

**PoC:** the smallest input/condition that triggers it (mark `(confirmed live)`
if reproduced against a running build).

**Fix:** the suggested remedy, concrete enough to act on.
```

Rules:

- **Title**: a noun phrase naming the defect, not the fix. Keep it under ~80
  chars so it fits the README priority list.
- **Location**: `file:line` (clickable). Update it if the code moves; the line
  numbers drift as fixes land, so re-anchor when you touch a finding.
- **Severity** and **Status** live on one line, separated by ` · `.
- Keep findings self-contained. If two findings are related, link by ID in
  prose ("root cause of SCAN-007", "gemello di UTF8-003"), don't merge them.

---

## 4. Severity levels

Use judgement; these are the rungs in use:

| Level | Meaning |
|---|---|
| **High** | Memory unsafety, crashes, silent wrong output/codegen, or anything exploitable. Fix first. |
| **Medium** | Real incorrect behavior in a normal path, or portability/perf cliffs. |
| **Low** | Edge-case correctness, poor diagnostics, benign-but-wrong behavior. |
| **Info** | Latent footguns, style/contract smells, things "safe today by luck". |

Add a parenthetical qualifier when useful: `Medium (portability)`,
`Low (cosmetic)`, `High (latent — no callers yet)`.

---

## 5. Status lifecycle

Allowed values: **OPEN**, **PARTIAL**, **FIXED**, **WONTFIX**.

```
OPEN ──► FIXED          (fully resolved and verified)
OPEN ──► PARTIAL ──► FIXED   (some sub-issues resolved, others remain)
OPEN ──► WONTFIX        (intended behavior / out of scope, with rationale)
```

- **Never delete a finding** to "resolve" it. Move its status instead. The
  history is the value. (If a finding turns out to be entirely mistaken, mark it
  `WONTFIX` and explain why it was a non-issue.)
- Status only moves toward resolution. If a "fixed" bug regresses, do **not**
  silently reopen — keep it `FIXED` if the regression is a *new* defect (give it
  a new ID), or, if the original fix was simply wrong, set it back to `PARTIAL`/
  `OPEN` with a dated note explaining the regression (see the SCAN-010 history
  for the pattern).
- Always stamp transitions with an absolute date: `**Status:** FIXED (2026-06-13)`.
  Never write relative dates ("yesterday", "last week").

### Closing a finding (→ FIXED)

Rewrite the body so the *current* state reads first, and archive the original
description in a collapsible block:

```markdown
## PREFIX-NNN — Title
**Severity:** <level> · **Status:** FIXED (YYYY-MM-DD) · **Location:** `file:line`

**Fix verified (<how>).** What changed and why it is correct now. Cite the new
`file:line`, show the key line(s), and state the observed result.

<details><summary>Original report</summary>

…the original OPEN text, verbatim…

</details>
```

`<how>` should state the evidence level — see §6.

### PARTIAL

Use when an attempted fix resolves part of a finding but leaves a real
remainder. Keep it open in the counts. Spell out exactly what is fixed and what
is not, with a reproducer for the remaining part. Downgrade a benign,
no-observable-impact remainder to a note under FIXED instead (see SCAN-011).

### WONTFIX

Record the rationale (ideally the author's own words) and keep the original
report collapsed. Example: a finding that turns out to describe intended
ownership semantics.

---

## 6. Verification discipline

State *how* a fix was verified, and prefer stronger evidence:

1. **Reproduced against a running build** — rebuild, then run the real binary on
   a minimal input and observe the error/behavior. Mark findings/PoCs
   `(confirmed live)`. This is the default bar for closing a finding.
2. **Standalone probe** — when the binary can't surface the detail (e.g. it
   stops before a phase, or you need exact bytes), link a tiny harness against
   the built library and print the raw result. This caught the SCAN-010
   byte-string regression that source-reading alone had rationalized away.
3. **By inspection** — acceptable only for changes whose effect can't be
   observed through available tooling (e.g. REPL-only paths, OOM branches). Say
   so explicitly: `(by inspection)`.

Project-specific cautions that generalize into a rule:

- **Do not trust stale specs.** Grammars, design docs, and old test suites may
  not reflect the real, intended behavior. When they conflict with the shipping
  code that everyone relies on (e.g. the standard library), the latter wins.
  Verify the actual artifact, not the documentation about it.
- **Re-test after every rebuild.** A stale binary will lie to you. Confirm the
  build actually recompiled the unit you changed before trusting a result.
- When a fix introduces or risks a regression, add a dated note to the finding
  describing what regressed and how the final version avoids it.

---

## 7. The README index

`README.md` is the dashboard and the shared coordination surface. Keep it in
sync on every change. It contains:

1. **Component table** — one row per report file:
   `| file | component (path + prefix) | findings (open/total) |`.
   The count is **open/total**; append `(+N WONTFIX)` when some are wontfixed,
   so `total = open + fixed + wontfix`.
2. **Top priorities** — the current High-severity / quick-win shortlist. Strike
   through (`~~…~~`) and tag with the fix date as items close, so the list
   doubles as a recent-progress log.
3. **Reviewed so far** — a dated log of which components have been audited, so
   nobody re-audits ground already covered. Check this before starting.

Whenever you change a finding's status, update that file's open/total count and,
if relevant, the priority list — in the same edit/commit.

---

## 8. Multi-agent operation

The format is designed so many agents can work concurrently with minimal
coordination. Rules that make that safe:

**Ownership**
- **One component file has one owner at a time.** An agent claims a component by
  adding/updating its row in the README "Reviewed so far" with its name and an
  `in-progress` marker before starting, or the orchestrator assigns components
  up front. Two agents must not edit the same `<component>.md` concurrently.
- Cross-file references are fine (read any file), but only the owner writes to a
  given component file.

**ID allocation without collisions**
- Because IDs are scoped per component file and only that file's owner writes to
  it, sequential allocation is conflict-free: the owner uses the next free
  `PREFIX-NNN` in its file. No global counter, no locking needed.
- If you discover a defect that belongs to a component you don't own, do **not**
  write into that file. Hand it to the owner / orchestrator (e.g. note it in
  your own file as "see also <component> — to be filed by its owner"), or file
  it yourself only after taking ownership.

**The README is shared mutable state**
- It is the one file multiple agents may need to touch. Serialize writes to it:
  prefer having the **orchestrator** apply README updates from each agent's
  reported deltas, or require agents to edit **only their own component's row**
  plus append-only entries (priorities, review log). Never rewrite another
  agent's row.
- Keep README edits small and localized so they merge cleanly.

**Atomicity & provenance**
- A finding is the unit of work: add or transition one complete finding block at
  a time. Never leave a half-written finding.
- Never renumber or delete existing findings (see §2/§5) — append-only history
  is what makes concurrent work safe to merge.
- Record provenance on transitions: date, evidence level (`confirmed live` /
  `probe` / `by inspection`), and — in a multi-agent run — which agent. This
  lets a reviewer audit who claimed what.

**Hand-off**
- When done with a component, set its review-log entry to a finished state and
  ensure its open/total count is exact. The next agent (or a verifier agent)
  relies on those numbers being trustworthy.

---

## 9. Quick recipes

**Start a new component report**
1. Pick a file name and a new `PREFIX`; create `issues/<component>.md`.
2. Add a header: title, the path(s) reviewed, the date, and the status legend.
3. Add a row to the README table and a dated line in "Reviewed so far".

**Add a finding**
1. Use the next `PREFIX-NNN` in that file.
2. Fill the open-finding template (§3); set severity and a real PoC.
3. If the bug has a runnable trigger, add a PoC file `poc/<component>/<id>.orb`
   (or `.cpp` for byte-level) per §11, encoding the expected post-fix behavior.
4. Bump the file's total (and open) count in the README.

**Mark a finding FIXED**
1. Verify (§6) — prefer live/probe.
2. Rewrite the body per §5 "Closing", archive the original in `<details>`,
   stamp the date and the new `file:line`.
3. Decrement the open count in the README; strike the item in priorities if
   listed.

**Mark PARTIAL / WONTFIX**
- PARTIAL: keep it open, enumerate fixed vs remaining with a reproducer.
- WONTFIX: record the rationale, collapse the original, exclude from open count
  (track as `+N WONTFIX`).

---

## 10. Standard component-file header

Every `<component>.md` starts with the same header block, then a `---` rule,
then findings. Paste this and fill the placeholders:

```markdown
# <component> — bug report

**Component:** `path/to/source` (file1, file2, …) · **ID prefix:** `PREFIX`
**PoCs:** [`poc/<component>/`](poc/<component>/) · **Last reviewed:** YYYY-MM-DD
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---
```

Keep "Last reviewed" current when you do a pass. `PREFIX` here is the same tag
used for this file's finding IDs (§2).

---

## 11. Proof-of-concept reproducers (`poc/`)

Every finding with a runnable trigger gets a PoC file so the whole set can be
re-run as a fast regression suite.

**Layout** — mirror the reports, one directory per component, one file per ID:

```
issues/poc/
  run.sh                # regression runner (see below)
  <component>/
    prefix-001.orb      # lowercase id, matches the finding ID
    prefix-003.cpp      # probe, when byte-level inspection is needed
```

File name = the finding ID in lowercase (`scan-001.orb`, `parse-007.orb`). One
PoC per ID; if a finding needs several inputs, pick the most representative
(note the siblings in a comment). Standalone project regression tests that
aren't tied to a single finding use a descriptive kebab-case name instead of an
ID (e.g. `ir/phi-regalloc.orb`).

**Two PoC kinds**

- **`.orb` source reproducer** — the minimal program that triggers the
  behavior. The first `# EXPECT:` line declares the expected outcome:
  - `# EXPECT: ok` — compiles cleanly (exit 0).
  - `# EXPECT: error` — compilation fails.
  - `# EXPECT: error: <substring>` — fails *and* the message contains
    `<substring>` (use the stable part of the diagnostic).
  - `# EXPECT: contains: <substring>` — *runs* (exit 0) and its output contains
    `<substring>`. For runtime/behavioral tests that execute (imports resolve
    via `ORBIT_PATH`) and print a success marker — e.g. `phi-regalloc.orb`
    prints `ALL TESTS PASSED`. Needed when the program exits 0 even on internal
    failure, so exit code alone isn't enough.
  Start the file with a one-line `# PoC <ID> — <what it checks>` comment.
- **`.cpp` self-checking probe** — for things the CLI can't surface (exact
  output bytes, internal status, phases that run before/after the CLI stops).
  Link against `libOrbiter`, assert, and **exit 0 on success / non-zero on
  failure** (print per-case PASS/FAIL for debugging). Used by SCAN-003
  (octal decode) and SCAN-010 (string-escape bytes). Get a ready Isolate from
  the shared `#include "../probe.h"` (`probe::isolate()`) so the runtime-init
  boilerplate lives in one place if that API shifts.

**No PoC** for findings that can't be triggered through available tooling
(REPL-only error recovery, OOM branches, location-tracking, big-endian-only
bugs). Those stay verified "by inspection"; don't force a misleading PoC.

**Encode the post-fix expectation.** A PoC asserts the *correct* (current)
behavior, so a FIXED finding's PoC passing == the fix still holds, and an OPEN
finding's PoC documents the target behavior (it may fail until the fix lands).

**Runner** — `issues/poc/run.sh [component]` runs every PoC and checks it against
its expectation; exits non-zero if any fail. **Rebuild the project first** so
`bin/Orbit` and the linked library reflect current code (a stale binary lies —
see §6). `.cpp` probes are compiled on the fly against `bin/include` +
`lib/stratum` and `bin/libOrbiter`.

---

## 12. Worked example

See `scanner.md` for the canonical reference: open findings, FIXED entries with
`<details>` archives, a PARTIAL→FIXED history (SCAN-003), a regression note
(SCAN-010), a benign-remainder FIXED (SCAN-011), and a WONTFIX with author
rationale (SCAN-017). `README.md` shows the matching index conventions, and
`poc/scanner/` shows the matching PoC set (both `.orb` and `.cpp` kinds).
