# utf8-stringbuilder (UTF-8 codec) — bug report

**Component:** `orbit/orbiter/datatype/stringbuilder.cpp` · **ID prefix:** `UTF8`
**PoCs:** _none yet_ — see [`poc/`](poc/) · **Last reviewed:** 2026-06-13
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

> Found while auditing the scanner's `\u`/`\U` escape path, which calls
> `StringIntToUTF8`.

---

## UTF8-001 — `StringIntToUTF8`: 2-byte branch emits invalid continuation byte
**Severity:** High · **Status:** FIXED (2026-06-13) · **Location:** `stringbuilder.cpp:449-453`

**Fix verified:** the 2-byte branch now emits `*buf = 0x80 | (glyph & 0x3F);`
(stringbuilder.cpp:451), producing a well-formed continuation byte. Cyrillic /
Latin-Ext-A codepoints now encode correctly.

<details><summary>Original report</summary>

**Location (original):** `stringbuilder.cpp:447-449`

```c
} else if (glyph < 0x0800) {
    *buf++ = glyph >> 6u & 0x1Fu | 0xC0u;
    *buf = glyph >> 0u & 0xBFu;        // BUG
}
```

The continuation byte must be `0x80 | (glyph & 0x3F)`. Masking with `0xBF`
instead takes bit 7 *from the codepoint*: the result is only valid when the
codepoint happens to have bit 7 set. Every 2-byte codepoint with bit 7 clear
encodes to **invalid UTF-8** (continuation byte in 0x00–0x3F range):

- U+0100–U+017F (Latin Extended-A: Ā, ē, ł, š…)
- U+0200–U+027F (Latin Extended-B / IPA)
- U+0300–U+037F (combining diacritics, Greek archaic)
- U+0400–U+047F (most of Cyrillic: А–я!)

**PoC:** `"А"` (Cyrillic А) → bytes `D0 10` instead of `D0 90`.
Compare: the 3- and 4-byte branches are correct (`& 0x3F | 0x80`).

**Fix:** `*buf = glyph & 0x3Fu | 0x80u;`

</details>

---

## UTF8-002 — `StringUTF8ToInt`: valid lead bytes 0xF1–0xF4 rejected + wrong shift
**Severity:** Medium · **Status:** FIXED (2026-06-13) · **Location:** `stringbuilder.cpp:483-497`

**Fix verified:** both parts resolved.

1. **Lead bytes `0xF1`–`0xF4`** — line 484 is now `if (*buf > 0xF4) return -1;`,
   so all valid 4-byte lead bytes (U+40000–U+10FFFF, emoji / supplementary
   planes) are accepted.
2. **Shift `<< 21` → `<< 18`** — line 488 now uses `<< 18`, decoding the
   4-byte payload correctly.

Optional hardening (not blocking): no explicit check that the assembled
codepoint is ≤ 0x10FFFF or that continuation bytes are well-formed — malformed
input still decodes to a garbage value rather than -1. Left as a minor
follow-up.

<details><summary>Original report</summary>

```c
if (*buf > 0xF0)
    return -1;
if ((*buf & 0xF0) == 0xF0)
    return (*buf & 0x07) << 21 | ...   // should be << 18
```

1. Lead bytes `0xF1`–`0xF4` are valid UTF-8 (codepoints U+40000–U+10FFFF,
   including most emoji planes beyond U+3FFFF) but were rejected as invalid.
2. The 4-byte decode shifted the lead-byte bits by 21 instead of 18.

</details>

---

## UTF8-003 — Endianness/aliasing cast shared with scanner
**Severity:** Medium (portability) · **Status:** OPEN · **Location:** `stringbuilder.cpp:121`

`StringIntToUTF8(*((unsigned int *) sequence), wb)` — same little-endian-only
type-punning pattern as SCAN-009 in `scanner.cpp:266`. Fix both the same way
(arithmetic accumulation).

---

## UTF8-004 — Surrogate codepoints not rejected
**Severity:** Low · **Status:** FIXED (2026-06-13) · **Location:** `stringbuilder.cpp:460-461`

**Fix verified:** `StringIntToUTF8` now returns 0 for
`glyph >= 0xD800 && glyph <= 0xDFFF` (checked before the 3-byte branch), so
surrogate codepoints can no longer be encoded into ill-formed UTF-8.

<details><summary>Original report</summary>

U+D800–U+DFFF are not valid scalar values and must not be UTF-8-encoded, but
`StringIntToUTF8` encoded them via the 3-byte branch. Combined with the
scanner's `\uD800` escapes this produced ill-formed UTF-8 strings (WTF-8) that
other consumers (validators, OS APIs) may reject.

</details>
