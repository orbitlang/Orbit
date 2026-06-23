# scanner — bug report

**Component:** `orbit/liftoff/scanner` (scanner.cpp, scanner.h, token.h) · **ID prefix:** `SCAN`
**PoCs:** [`poc/scanner/`](poc/scanner/) · **Last reviewed:** 2026-06-15
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## SCAN-001 — String literals starting with `#` are mis-lexed
**Severity:** High · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:556-557` (`TokenizeString`)

**Fix verified (reproduced against `bin/Orbit`):** the opening-hash counting
loop is now guarded by `if (check_prefix)`, so it only runs on the raw/byte
string path. `x := "#tag"` now compiles instead of throwing "unterminated
string".

<details><summary>Original report</summary>

**Location (original):** `scanner.cpp:556` (`TokenizeString`)

The opening-hash counting loop runs unconditionally, even when the string is a
normal `"`-delimited string (`check_prefix == false`, called from `NextToken`
case `'"'`):

```c
// Count beginning hashes
for (; this->Peek() == '#'; this->Next(), hashes++);
```

For `"#hello"` the `#` *inside* the string is counted as a raw-string hash
(`hashes = 1`). When the closing `"` is found, `count (0) != hashes (1)`, so the
quote is re-embedded as content and scanning continues past the end of the
literal, swallowing the rest of the line/file until EOF (`unterminated string`)
or until a spurious `"#` match.

**PoC:** `let s = "#tag"` → scanner error / wrong tokenization.

</details>

---

## SCAN-002 — Empty inline comment swallows the newline and the next line
**Severity:** High · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:377-380` (`TokenizeComment`)

**Fix verified (reproduced against `bin/Orbit`):** the skip loop now uses
`isblank(skip)` instead of `isspace(skip)`, so inline comments no longer
consume the trailing newline. `x := 1 #` followed by `y := 2` now parses both
statements (codegen emits the store for `y`).

<details><summary>Original report</summary>

```c
for (int skip = this->Peek();
     isspace(skip) || (!inline_comment && skip == '\n');
     this->Next(), skip = this->Peek());
```

`isspace()` already matches `'\n'`, so the second clause is dead and inline
(`#`) comments also skip newlines at comment start. If an inline comment body
is empty or whitespace-only (`#` alone on a line, `#   \n`), the loop consumes
the line terminator and the *next line's code becomes the comment body*. No
`END_OF_LINE` token is emitted, so two statements get merged.

**PoC:**
```
x := 1 #
y := 2
```
`y := 2` is consumed as comment text.

</details>

---

## SCAN-003 — Octal escapes decode incorrectly
**Severity:** High · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:221-245` (`ParseOctEscape`)

**Fix verified (built + reproduced against `bin/Orbit`):** `ParseOctEscape` was
rewritten to accumulate left-to-right, which is correct for 1/2/3 digits, and
now also range-checks the result:

```c
unsigned int byte = HexDigitToNumber(value);
for (int i = 0; i < 2 && IsOctDigit(this->Peek()); i++)
    byte = (byte << 3) | HexDigitToNumber(this->Next());

if (byte > 0xFF) {
    this->status = ScannerStatus::INVALID_OCT_BYTE;   // new status, added 2026-06-13
    return false;
}
```

`\7` → 7, `\50` → 40, `\100` → 64, `\101` → 65 all decode correctly. The
**Related (Low)** overflow gap is also closed: `\400`–`\777` now raise
`INVALID_OCT_BYTE` ("octal escape value out of range, must be between \000 and
\377"), confirmed live.

<details><summary>Original report</summary>

```c
for (int i = 0, mul = 0; i < 3; i++) {
    byte |= sequence[i] << (unsigned char) (mul * 3);
    if (sequence[i] != 0)
        mul++;
}
```

`mul` was only incremented for non-zero digits. That trick made short
sequences (`\5`) work, but corrupted any sequence with an interior or trailing
`0` digit because the positional weight collapsed:

- `\100` → 1 (expected 64, `'@'`)
- `\101` → 9 (expected 65, `'A'`)
- `\50` → 5 (expected 40, `'('`)

A first remediation attempt (unconditional `mul++`) fixed the 3-digit cases but
regressed 1-digit (`\7` → 192) and left 2-digit wrong; the final left-to-right
rewrite above resolves all cases.

</details>

---

## SCAN-004 — `0x` / `0b` / `0o` with no digits produce empty number tokens
**Severity:** Medium · **Status:** FIXED (2026-06-13) · **Location:** `TokenizeBinary` / `TokenizeOctal` / `TokenizeHex` (`scanner.cpp:322, 463, 536`)

**Fix verified (built + reproduced against `bin/Orbit`):** each radix tokenizer
now rejects an empty literal by folding `this->sbuf_.GetLength() == 0` into its
existing trailing-character check, reusing the per-radix status:

```c
if (this->sbuf_.GetLength() == 0 || isdigit(value)) {            // binary / octal
    this->status = ScannerStatus::INVALID_BINARY_LITERAL;        // (resp. ..._OCTAL_...)
    return false;
}
if (this->sbuf_.GetLength() == 0 || (isalpha(value) && value != 'u' && value != 'U')) {  // hex
    this->status = ScannerStatus::INVALID_HEX_LITERAL;
    return false;
}
```

`0x`, `0b`, `0o`, and `0xu` now raise the matching "invalid … literal" error;
valid literals (`0xFF`, `0b101`, `0o17`, `0xFFu`) still compile. (The prefix
chars aren't pushed into `sbuf_`, so `GetLength() == 0` means "zero digits".)

<details><summary>Original report</summary>

None of the radix tokenizers required at least one digit. `0x` followed by
EOF/space/newline yielded a `NUMBER_HEX` token with `buffer == nullptr`,
`length == 0`. Downstream conversion saw an empty literal (likely 0 or a crash
depending on the parser).

Also inconsistent: `0b2` errored (`isdigit` trailing check) but `0bf` lexed as
*empty binary number* + identifier `f`.

**PoC:** `let a = 0x` → accepted by the scanner.

</details>

---

## SCAN-005 — Inline comment at EOF (no trailing newline) is a lexing error
**Severity:** Medium · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:397-399` (`TokenizeComment`)

**Fix verified (built + reproduced against `bin/Orbit`):** the EOF guard is now
`if (peek <= 0 && !inline_comment) return false;`, so EOF terminates an inline
comment normally and the error is kept only for unterminated block comments. A
file ending in `# comment` without a trailing newline now scans cleanly.

<details><summary>Original report</summary>

The loop exits with `peek <= 0` at EOF, and `if (peek <= 0) return false;`
treated that as an error even for inline comments, where EOF is a perfectly
valid terminator. A source file whose last line was `# comment` *without* a
final newline failed to scan (status `END_OF_FILE`).

</details>

---

## SCAN-006 — `/**/` (empty doc comment) lexes as unterminated comment
**Severity:** Medium · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:794-806` (`NextToken`, case `'/'`)

**Fix verified (built + reproduced against `bin/Orbit`):** the doc-comment
branch now peeks one char past the second `*`; if it is `/` (i.e. the input is
`/**/`), the closing `/` is consumed and a plain empty `TokenType::COMMENT` is
emitted instead of delegating to `TokenizeComment`. `/**/` followed by code now
compiles; `/***/`, `/*!*/`, real doc comments, and normal block comments are
unaffected.

Emitting a plain `COMMENT` (not `COMMENT_DOC`) is deliberate: an empty `/**/`
carries no documentation, and routing it to the doc path would feed an empty
buffer into `GetDocString` (see still-open PARSE-002).

<details><summary>Original report</summary>

For `/**/`: the scanner consumes `/`, `*`, then sees the second `*` and
consumes it as the doc-comment marker. `TokenizeComment` then reads `/` as
content — the closing `*` is already gone — and scans to EOF → error. Same
shape, `/*!*/` and `/***/`, work fine; only the exactly-empty doc comment
breaks.

</details>

---

## SCAN-007 — NUL byte in source silently truncates the input
**Severity:** Medium · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:916-957` (`Scanner::Peek`) + `ibuffer.cpp:138` + `scanner.cpp:895`

**Fix verified (built + reproduced against `bin/Orbit`).** Three coordinated
changes, all required:

1. `InputBuffer::Peek` now returns **-1** on exhaustion (`ibuffer.cpp:138`)
   instead of 0, so a real `0x00` byte is distinguishable from end-of-buffer
   (this is the IBUF-002 root-cause fix).
2. `Scanner::Peek` checks `chr == 0` **inside** the refill loop, before the
   `fd_ == nullptr` / `ReadFile` paths, and raises the new
   `ScannerStatus::INVALID_NULLBYTE` (`scanner.cpp:924-928`). Placing it after
   the loop (first attempt) was dead code — the exhaustion paths intercepted
   `chr == 0` first.
3. `NextToken` no longer emits `END_OF_FILE` unconditionally when `Peek() <= 0`:
   `if (this->status != ScannerStatus::END_OF_FILE) return false;`
   (`scanner.cpp:895`), so the NUL error (and a latent NOMEM-as-EOF swallow) is
   propagated instead of masked as a clean EOF.

Leading, interior, and trailing NUL bytes now all raise *"null-byte '\0' is not
allowed in source code"*; NUL-free files still compile and code after the
former truncation point is no longer dropped.

<details><summary>Original report</summary>

`InputBuffer::Peek` returned the raw byte; a `0x00` byte in the source was
indistinguishable from "buffer exhausted" (returned 0). `Scanner::Peek` treated
it as underflow → `END_OF_FILE`. Everything after the first NUL byte was
silently ignored, with no error.

Security angle: source-smuggling — a file can carry code visible to humans /
other tools that the compiler silently never sees.

</details>

---

## SCAN-008 — `sbuf_` not cleared on tokenize failure (stale bytes leak into next token)
**Severity:** Medium · **Status:** FIXED (2026-06-13) · **Location:** `NextToken` (`scanner.cpp:690`) + `sbuffer.cpp:99` (`StoreBuffer::Clear`)

**Fix verified (by inspection; REPL-only PoC not reproducible in batch CLI).**
A new `StoreBuffer::Clear()` (`sbuffer.cpp:99-101`) rewinds `cursor_ = buffer_`
— discarding any partial content while keeping the allocated capacity, and
safely a no-op when `buffer_ == nullptr`. `NextToken` now calls `sbuf_.Clear()`
at the top (`scanner.cpp:690`), so bytes left over from a failed tokenization
can no longer prefix the next token. (Batch compilation aborts on the first
scanner error, so the cross-token leak only manifested in REPL error recovery;
the fix is verified by code review.)

<details><summary>Original report</summary>

The only thing that reset `StoreBuffer` was the ownership transfer in
`GetBuffer()`, which happens on *success*. Every failure path (e.g.
`TokenizeBinary` after consuming `1` of `0b12`) left the partial bytes in
`sbuf_`. If the caller kept scanning after an error (REPL error recovery), the
next token's buffer was prefixed with the stale bytes.

**PoC (REPL):** enter `0b12`, get error, enter `foo` → token buffer is `"1foo"`.

</details>

---

## SCAN-009 — `ParseUnicode` is little-endian-only and violates strict aliasing
**Severity:** Medium (portability) · **Status:** OPEN · **Location:** `scanner.cpp:266`

```c
const int len = orbiter::datatype::StringIntToUTF8(*((unsigned int *) sequence), buf);
```

Bytes are stored as `sequence[(width-1)-i] = byte` and then reinterpreted as a
`u32` — correct only on little-endian hosts; on big-endian every `\u`/`\U`
escape decodes to the wrong codepoint. The cast is also undefined behavior
(strict aliasing, alignment is fine only by luck of the stack layout).

**Fix:** build the value arithmetically: `value = (value << 8) | byte` inside
the read loop. (Same pattern exists in `stringbuilder.cpp:121` — see
`utf8-stringbuilder.md`.)

Note: surrogate codepoints (`\uD800`–`\uDFFF`) are not rejected here either;
`StringIntToUTF8` happily encodes them (see UTF8-004).

---

## SCAN-010 — Raw strings (`r"..."`) still process escape sequences
**Severity:** Medium · **Status:** FIXED (2026-06-13; first attempt regressed byte strings, then corrected) · **Location:** `scanner.cpp:634-639` (`TokenizeString`)

**Final fix (verified with a standalone scanner probe — exact output bytes).**
A string is *raw* (backslashes verbatim) when it has the `r` prefix **or** when
it is hash-delimited (`hashes > 0`); everything else processes escapes:

```c
// raw = (check_prefix && !byte_string)  ||  hashes > 0
if ((!check_prefix || byte_string) && hashes == 0 && value == '\\') { ... }
```

- normal `"..."`: escapes incl. `\u`/`\U`.
- plain byte `b"..."`: escapes with `\u`/`\U` ignored (`ignore_unicode`).
- `r"..."`, `r#"..."#`, `b#"..."#`: backslashes verbatim.

The `hashes == 0` term is essential: `check_prefix`/`byte_string` alone cannot
tell `b"..."` (escapes) from `b##"..."##` (raw) — both have `byte_string == true`.
Rawness of the hash forms is decided by the hash count, not the prefix letter.

Probe output:
`"a\nb"` → `61 0A 62`; `b"a\nb"` → `61 0A 62`; `b#"a\nb"#` → `61 5C 6E 62`;
`r"a\nb"` → `61 5C 6E 62`; `r#"a\nb"#` → `61 5C 6E 62`. (Plus `b"\n"` → `0A`,
`b"\x41"` → `41`.)

> **Regression caught (2026-06-15):** the first attempt guarded the escape
> branch with just `!check_prefix`, which disabled escapes for **byte strings**
> too (since `check_prefix` is true for both `r` and `b`). The grammar
> (`grammar.ebnf:47`) describes `bytes_string` as raw, but that grammar is stale
> — the stdlib relies on byte-string escapes: `io.print`/`perror` default their
> terminators to `end=b"\n"`, `sep=b" "` (`stdlib/io/io.orb:278,320`). With the
> regression `b"\n"` lexed as the two bytes `\`,`n`, so `io.print("ciao\n")`
> emitted a real newline (from the normal string) followed by a literal `\n`
> (from `end`), and `io.input` never matched the 0x0A line terminator. The
> corrected condition restores byte-string escapes while keeping `r"..."` raw.

<details><summary>Original report</summary>

The `\\` escape handling ran regardless of whether the string was introduced
with the `r` prefix or hashes. `r"C:\temp\new"` interpreted `\t` and `\n`. Raw
strings should keep backslashes verbatim; byte and normal strings should not.

</details>

---

## SCAN-011 — `ParseEscape`: lost NOMEM + garbage byte at EOF
**Severity:** Low · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:195-199`

**Fix verified.** Part 1 (the substantive bug) is resolved: the default branch
now chains both writes —
`if (!ok || !this->sbuf_.PutChar((unsigned char) value)) { ok = false; break; }`
— so an allocation failure on the second `PutChar` is propagated as NOMEM
instead of silently truncating the token.

Benign residual (part 2): an EOF immediately after a backslash still pushes a
transient `\` + `0xFF` into `sbuf_`, but it is always discarded — both callers
(`TokenizeString`, `TokenizeChar`) then hit EOF and error out (verified live:
a string ending in `\` reports "unterminated string", no crash), and `Clear()`
wipes the buffer next round. Fully eliminating it would just need an early
`value <= 0` reject; left as a no-impact nicety.

<details><summary>Original report</summary>

```c
ok = this->sbuf_.PutChar('\\');
if (ok)
    this->sbuf_.PutChar((unsigned char) value);   // result discarded
```

1. The second `PutChar`'s failure was ignored → on allocation failure the
   token was silently truncated instead of reporting NOMEM.
2. If `Next()` returned -1 (EOF right after a backslash), the default branch
   stored `\` + `0xFF` before the enclosing tokenizer eventually errored out.

</details>

---

## SCAN-012 — `123abc` lexes as two tokens (no trailing-character check for decimals)
**Severity:** Low · **Status:** FIXED (2026-06-13) · **Location:** `TokenizeDecimal` (`scanner.cpp:466-470`)

**Fix verified (built + reproduced against `bin/Orbit`).** After the
digit/fraction loop `TokenizeDecimal` now rejects a trailing letter (other than
the `u`/`U` suffix):

```c
if (isalpha(this->Peek()) && this->Peek() != 'u' && this->Peek() != 'U') {
    this->status = ScannerStatus::INVALID_NUMBER_LITERAL;   // new status
    return false;
}
```

`123abc` now errors ("invalid digit in number"); `123`, `123u`, and `1.5` still
compile. New `ScannerStatus::INVALID_NUMBER_LITERAL` (enum index 9) and its
message ("invalid digit in number") are correctly aligned in `GetStatusMessage`.

<details><summary>Original report</summary>

Binary/octal/hex literals validated the character following the literal;
decimal did not. `123abc` → `NUMBER(123)` + `IDENTIFIER(abc)` with no error,
deferring a confusing diagnostic to the parser.

</details>

---

## SCAN-013 — Lone `@` produces an empty ATOM token
**Severity:** Low · **Status:** FIXED (2026-06-13) · **Location:** `TokenizeAtom` (`scanner.cpp:311-315`)

**Fix verified (built + reproduced against `bin/Orbit`).** `TokenizeAtom` now
errors when no atom characters were collected:

```c
if (this->sbuf_.GetLength() == 0) {
    this->status = ScannerStatus::INVALID_TK;
    return false;
}
```

Lone `@` now errors ("invalid token"); `@atom` and `@[` (decorator, handled
earlier in `NextToken`) are unaffected.

<details><summary>Original report</summary>

`TokenizeAtom` had no minimum-length requirement: `@` followed by anything
non-alphanumeric yielded `ATOM` with `length == 0`, `buffer == nullptr`.

</details>

---

## SCAN-014 — `Scanner::Peek(advance=true)` advances source location at EOF
**Severity:** Low (cosmetic) · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:992-993`

**Fix verified (by inspection).** The advance is now guarded with
`if (advance && chr > 0) this->loc.Advance(chr == '\n');`, so location no longer
advances on EOF/NOMEM (`chr <= 0`). Combined with the SCAN-007 restructuring
(all `chr <= 0` paths return early inside the refill loop), location tracking
stays accurate at end-of-file.

<details><summary>Original report</summary>

```c
if (advance)
    this->loc.Advance(chr == '\n');
```

This ran even when `chr == -1` (EOF/NOMEM), so every `Next()` at EOF inflated
`column`/`offset`. Error locations at end-of-file drifted past the real end.

</details>

---

## SCAN-015 — `DefaultPrompt`: 1-byte heap underread on NUL input
**Severity:** Low · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp:126`

**Fix verified (built).** The loop condition now short-circuits on `cur == 0`:
`} while (cur == 0 || *((buf + cur) - 1) != '\n');` — so `buf[-1]` is never read
when `fgets` returns a line whose first byte is NUL (`strlen == 0`). The loop
re-reads instead (terminating at a real newline or EOF), so the OOB read is
gone. Embedded-NUL interactive lines are still degenerate, but are now rejected
downstream by the SCAN-007 NUL check rather than causing UB here.

<details><summary>Original report</summary>

`cur += strlen(buf + cur)` then `while (*((buf + cur) - 1) != '\n')`. If
`fgets` read a line that *started* with a NUL byte, `strlen` was 0, `cur` stayed
0, and the loop condition read `buf[-1]` (heap underread).

</details>

---

## SCAN-016 — Successful `NextToken` can leave `status == END_OF_FILE`
**Severity:** Info · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp` (`NextToken` wrapper) + `scanner.h`

**Fix verified (built; EOF-terminated token compiles).** The tokenizing body
was renamed to `NextTokenInternal`, and the public `NextToken` is now a thin
wrapper that normalizes the status on success:

```c
bool Scanner::NextToken(Token *out_token) noexcept {
    if (!this->NextTokenInternal(out_token))
        return false;
    this->status = ScannerStatus::GOOD;   // a valid token always reads GOOD
    return true;
}
```

So a lookahead `Peek` past the last character (which sets `END_OF_FILE` as a
side effect) no longer leaves a misleading status after a successful token —
including the `END_OF_FILE` *token* itself, which is signalled by its
`TokenType`, not by `status`.

<details><summary>Original report</summary>

A tokenizer that ended by peeking past the last char (e.g. identifier at EOF)
set `status = END_OF_FILE` as a side effect while the call still returned
`true`. Any caller checking `status` instead of the return value got a false
error.

</details>

---

## SCAN-017 — `Token` move-assignment footguns
**Severity:** Info · **Status:** WONTFIX (2026-06-13) · **Location:** `token.h:318-329`

**Not a bug — by design (per author).** Move-assignment intentionally *moves*
the buffer to the recipient; the recipient owns it and frees it on destruction
(unless it is moved on again). If the buffer survives into the next
`NextToken`, it is freed explicitly there (now via the correct isolate — see
SCAN-018). This is the intended ownership protocol, not a leak.

(Possible future tweak under consideration by the author: have `NextToken` do a
`Clear()` rather than a `free`, pending a check that it doesn't disturb other
consumers — the parser in particular.)

<details><summary>Original report</summary>

`operator=(Token&&)` does not free the destination's existing `buffer` and does
not reset `other.type`; flagged as relying on call-site workarounds. Per the
author this is the intended move-ownership behavior, not a defect.

</details>

---

## SCAN-018 — `NextToken` frees the token buffer with the scanner's isolate
**Severity:** Info · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp` (`NextTokenInternal` cleanup)

**Fix verified (built).** The previous-buffer cleanup now uses
`out_token->isolate` (the isolate that allocated it), and the
`out_token->isolate = this->isolate_;` assignment was moved to *after* the free
so the old isolate is still available:

```c
if (out_token->buffer != nullptr) {
    const orbiter::memory::IsolateAllocator allocator(out_token->isolate);
    allocator.free(out_token->buffer);
    out_token->buffer = nullptr;
    out_token->length = 0;
}
out_token->isolate = this->isolate_;
```

This matches `Token`'s own destructor (which frees via `this->isolate`), so a
token recycled across scanners with different isolates is freed with the right
allocator.

<details><summary>Original report</summary>

The cleanup used `this->isolate_`'s allocator and ignored `out_token->isolate`.
A token recycled across scanners with different isolates would be freed with the
wrong allocator.

</details>

---

## SCAN-019 — `\r` handling quirks
**Severity:** Info · **Status:** FIXED (2026-06-13) · **Location:** `scanner.cpp` (`NextToken` `\r`/`\n`/`\\` cases)

**Fix verified (built + reproduced against `bin/Orbit`).** The `\r` case now
falls through to a shared loop that collapses a run of consecutive newlines,
mixing `\n` and `\r\n` uniformly into a single `END_OF_LINE` (so `\r\n\r\n` no
longer yields two tokens). Line continuation accepts `\`+`\r\n` as well as
`\`+`\n`. A lone `\r` (not followed by `\n`) remains `INVALID_TK` by design.

Confirmed live: CRLF files compile; mixed blank-line runs (`\r\n\r\n\n`)
collapse; CRLF and LF line continuations both work; a lone `\r` errors.

<details><summary>Original report</summary>

Lone `\r` was `INVALID_TK`; `\n` collapsed runs of newlines into one
`END_OF_LINE` while `\r\n\r\n` produced two tokens (parser inconsistency); line
continuation `\` + `\r\n` was rejected (only `\` + `\n` accepted).

</details>
