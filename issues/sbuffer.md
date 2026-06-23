# sbuffer (StoreBuffer) — bug report

**Component:** `orbit/liftoff/scanner` (sbuffer.cpp, sbuffer.h) · **ID prefix:** `SBUF`
**PoCs:** [`poc/sbuffer/`](poc/sbuffer/) · **Last reviewed:** 2026-06-15
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

Overall this component is sound — `Enlarge` correctly reserves the +1 NUL
slot, `PutString` relies on `MemoryCopy` returning `dest + size` (verified in
`lib/stratum/stratum/memutil.cpp:42` — it does, unlike libc `memcpy`).

---

## SBUF-001 — fixed +8 growth → O(n²) for large tokens
**Severity:** Medium (perf) · **Status:** FIXED (2026-06-15) · **Location:** `sbuffer.cpp` (`Enlarge` + `GetBuffer`)

**Fix verified (built; correctness probe `poc/sbuffer/sbuf-001.cpp`).** `Enlarge`
now grows the capacity geometrically (`newcap = max(capacity*2, used+increase)`)
instead of by a fixed 8, and `GetBuffer` does a **shrink-to-fit** `realloc` to
exactly `length + 1` on hand-off — so building a token is amortized O(n) while
the value's permanent storage stays the exact content size (no doubling waste).

Why the original was genuinely O(n²) (verified against Stratum, not assumed):
`Memory::Realloc` only returns the same block without copying when *shrinking*
within a threshold (`memory.cpp:213`); every *grow* does `Alloc + MemoryCopy`.
Stratum's size classes are 8-byte granular (`SizeToPoolClass`, QUANTUM=8), so a
fixed `+8` step crossed a class boundary on **every** `Enlarge` → a copy every 8
bytes → 8+16+…+N ≈ O(N²). (Negligible for small tokens; a real cliff for
multi-KB/MB string literals and block comments, which also build via `PutChar`.)

Geometric-only growth was rejected because `GetBuffer` hands the block off as the
token's permanent storage, so doubling alone would leave every literal in a block
up to 2× its content; the shrink-to-fit step is what keeps memory tight. On a
realloc-down failure Stratum leaves the original intact, so `GetBuffer` falls
back to it.

<details><summary>Original report</summary>

Every time the buffer filled, capacity grew by exactly 8 bytes and `realloc`
copied the whole buffer. A large string literal built char-by-char (the common
path — `TokenizeString` calls `PutChar` per byte) cost O(n²): a 1 MiB literal
implied ~131k reallocs, each copying up to 1 MiB.

</details>

---

## SBUF-002 — Signed/unsigned comparison in `Enlarge`
**Severity:** Info (fragile, not currently exploitable) · **Status:** FIXED (2026-06-15) · **Location:** `sbuffer.cpp` (`Enlarge`)

**Fix verified (built).** The remaining-space computation is now an explicit
`const size_t available = this->end_ - this->cursor_;` (with a comment noting the
`cursor_ <= end_` invariant), and the grow test is `available < increase` — no
implicit `ptrdiff_t`→`size_t` conversion in the comparison.

<details><summary>Original report</summary>

`(this->end_ - this->cursor_) < increase` compared a `ptrdiff_t` against a
`size_t`; the signed side was converted to unsigned. `cursor_ <= end_` is an
invariant so the difference was never negative, but any future violation would
silently skip the grow instead of failing loudly.

</details>

---

## SBUF-003 — Missing `Reset()` API forces leak-prone usage on error paths
**Severity:** Info (enabler of SCAN-008) · **Status:** FIXED (2026-06-13) · **Location:** `sbuffer.cpp:99-101` (`StoreBuffer::Clear`)

**Fix verified.** Resolved together with SCAN-008: `StoreBuffer::Clear()` rewinds
`cursor_ = buffer_` (keeping the allocated capacity, no-op when `buffer_` is
null), and `NextToken` calls it on entry, so failure paths no longer leave
partial content for the next token.

<details><summary>Original report</summary>

The only way to "empty" the buffer was `GetBuffer`, which transfers ownership.
The scanner's failure paths couldn't cheaply discard partial content — the root
enabler of SCAN-008. The ask was a `Reset()` rewinding `cursor_ = buffer_`.

</details>
