# ibuffer (InputBuffer) — bug report

**Component:** `orbit/liftoff/scanner` (ibuffer.cpp, ibuffer.h) · **ID prefix:** `IBUF`
**PoCs:** [`poc/ibuffer/`](poc/ibuffer/) · **Last reviewed:** 2026-06-15
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## IBUF-001 — `GetCurrentLine`: OOB read + size_t underflow → huge memcpy
**Severity:** High (latent — no callers in tree today) · **Status:** OPEN · **Location:** `ibuffer.cpp:58-78`

String-mode branch (`last_line_ == nullptr`):

```c
length = this->b_cur_ - this->ll_end_;
if (this->buffer_[this->ll_end_] == '\n')
    length--;
if (length == 0)
    return nullptr;
```

Two stacked problems:

1. **OOB read:** after consuming a trailing `'\n'`, `Peek(true)` sets
   `ll_end_ = b_cur_ + 1`, so once the newline is consumed `ll_end_ == b_cur_`,
   and if the newline was the last byte, `ll_end_ == b_length_`. The check
   `buffer_[ll_end_]` then reads one byte past the buffer — which in string
   mode is *caller-owned memory of exact length* (non-owned pointer ctor).
2. **Underflow:** when `length == 0` and `buffer_[ll_end_] == '\n'` (e.g. the
   character after a just-consumed newline is another newline — blank line),
   `length--` underflows `size_t` to `SIZE_MAX`. The `length == 0` guard then
   passes, `*out_len` becomes -1, and `memcpy(line, ..., SIZE_MAX)` segfaults.

**PoC:** scan `"a\n\nb"`, consume through the first `\n`, call
`GetCurrentLine` → length underflow path.

**Fix:** guard the order: `if (length == 0) return nullptr;` *before* the
newline adjustment, and bound-check `ll_end_ < b_length_` before indexing.

---

## IBUF-002 — `Peek` cannot distinguish a NUL byte from buffer exhaustion
**Severity:** Medium · **Status:** FIXED (2026-06-13) · **Location:** `ibuffer.cpp:138`

**Fix verified:** `Peek` now returns **-1** on exhaustion instead of 0, so a
legitimate `0x00` byte (returned as 0) is distinguishable from end-of-buffer.
The scanner side (`Scanner::Peek` + `NextToken`) consumes this distinction to
reject NUL bytes explicitly — see SCAN-007 for the full three-part fix and
live verification.

<details><summary>Original report</summary>

`Peek` returned `buffer_[b_cur_]` directly and used `0` as the "no data"
sentinel. A legitimate `0x00` byte in the source therefore looked like
exhaustion, and the scanner turned it into `END_OF_FILE`: everything after the
first NUL byte was silently dropped.

</details>

---

## IBUF-003 — `ReadFile` reports allocation failure as EOF
**Severity:** Low · **Status:** FIXED (2026-06-15) · **Location:** `ibuffer.cpp:147-156`

**Fix verified (by inspection — OOM path not triggerable through normal input).**
Both `alloc` failure paths now `return -1;` instead of `return false;` (0), so
`Scanner::Peek` maps them to `NOMEM` rather than a silent `END_OF_FILE`.

<details><summary>Original report</summary>

Both `alloc` failure paths did `return false;` in a function returning `int` —
i.e. `0`, which `Scanner::Peek` interpreted as `END_OF_FILE`. An OOM during the
first read surfaced as a silent empty file instead of `NOMEM`.

</details>

---

## IBUF-004 — Read errors reported as EOF; partial data discarded on error
**Severity:** Low · **Status:** FIXED (2026-06-15) · **Location:** `ibuffer.cpp:144-179` + `fd_error_` flag (`ibuffer.h:31`)

**Fix verified (by inspection — `ferror` not triggerable through normal input).**
The clean-EOF and error cases are now separated, and partial data is delivered
before the error is reported:

```c
if (this->fd_error_) return -1;            // error from a prior call
...
if (read == 0 && feof(fd) != 0) return 0;  // clean EOF
if (ferror(fd) != 0) {
    if (read == 0) return -1;              // error, no data → report now
    this->fd_error_ = true;               // error + partial data → deliver, defer error
}
this->b_length_ = read; this->b_cur_ = 0;
return read;
```

So an I/O error is no longer indistinguishable from EOF, and bytes already read
in the failing call are consumed before the next underflow returns the error.

`ReadFile` now uses **distinct error returns** — `-1` = OOM, `-2` = I/O error
(both `ferror` paths) — and `Scanner::Peek` maps them to `ScannerStatus::NOMEM`
vs the new `ScannerStatus::IO_ERROR` ("input/output error while reading
source"). So an I/O read error finally gets a truthful message instead of
"not enough memory".

**Confirmed live** (`poc/ibuffer/ibuf-004.cpp`): opening a directory as a file
makes `fread` fail with `ferror`; the scanner reports `IO_ERROR`, not EOF/NOMEM.

<details><summary>Original report</summary>

```c
if (ferror(fd) != 0 || read == 0 && feof(fd) != 0)
    return 0;
```

`ferror` → `return 0` = EOF: an I/O error was indistinguishable from a clean end
of file, and any bytes `fread` *did* return in that call were thrown away
(`b_length_`/`b_cur_` not updated).

</details>

---

## IBUF-005 — `b_length_` permanently shrinks after a short read
**Severity:** Low (perf) · **Status:** FIXED (2026-06-15) · **Location:** `ibuffer.cpp` (`ReadFile`) + `ibuffer.h`

**Fix verified (by inspection — perf-only, mid-stream short reads aren't
deterministically reproducible; the file-mode `.orb` PoCs exercise the rewritten
`ReadFile` end-to-end without regression).** Capacity and valid-length are now
separate fields: `b_capacity_` (allocated size, constant) and `b_length_` (bytes
actually read = Peek limit). `ReadFile` allocates and `fread`s `b_capacity_` and
sets `b_length_ = read`, so a short read no longer shrinks the buffer for
subsequent reads.

<details><summary>Original report</summary>

`b_length_ = read;` repurposed the *capacity* field as the data length. After
any short read (pipes, ttys, EINTR), every subsequent `fread` requested only the
shrunken size — a 4 KiB buffer could degrade to tiny reads for the rest of the
stream. Functionally correct, just wasteful; needed a separate capacity vs.
valid-length field.

</details>

---

## IBUF-006 — `AppendInput`: unbounded growth + O(n²) realloc pattern
**Severity:** Low (REPL only) · **Status:** FIXED (2026-06-15) · **Location:** `ibuffer.cpp` (`AppendInput`)

**Fix verified (correctness probe `poc/ibuffer/ibuf-006.cpp`).** `AppendInput`
now reclaims consumed input and grows geometrically:

- **Compaction:** when `b_cur_ > 0` it drops the consumed prefix (shifting the
  live remainder to the front and adjusting `ll_end_`). An interactive underflow
  only fires once all prior input is consumed (`b_cur_ == b_length_`), so the
  remainder is normally empty and this is effectively a reset — the buffer is
  reused instead of growing every line, bounding a REPL session to ~the largest
  line. The copy is kept (dest < src, forward-safe) for robustness.
- **Geometric growth:** capacity doubles (`b_capacity_`) instead of the old
  `+= length` per call, so n appends are amortized O(n) rather than O(n²).

The probe runs 2000 append→consume cycles (exercising the compaction-reset every
iteration) and verifies byte integrity throughout. (Bounded-memory itself is by
inspection — `b_capacity_` is private.)

<details><summary>Original report</summary>

Consumed input (`b_cur_`) was never compacted, so a long REPL session grew the
buffer forever. Growth was also by exactly `length` per call, so n appends cost
O(n²) in realloc copies. The `b_wr_` field has been folded into `b_length_`
(valid-data length) and `b_capacity_` added for allocated size.

</details>

---

## IBUF-007 — `last_line_` ring wrap reconstructs a garbled line for lines > 1 KiB
**Severity:** Low (diagnostics only) · **Status:** OPEN · **Location:** `ibuffer.cpp:112-129` + `GetCurrentLine` file branch

When the current line exceeds `ll_size_` the wrap logic does
`if (ll_end_ == ll_size_) ll_end_ = ll_size_ / 2;` — an arbitrary halving that
drops a chunk of the line and leaves `ll_end_` pointing mid-data.
`GetCurrentLine` then stitches `[ll_end_, ll_size_) + [0, ll_cur_)` producing a
line with a silent gap / wrong prefix. Only affects error-message display for
very long lines, but the reconstruction is simply incorrect. Consider storing
an explicit "line truncated" flag instead.
