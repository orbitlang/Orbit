# `number.md` — `orbit/orbiter/datatype/number.cpp` (integer Number: parsing & representation)

Findings for Orbit's integer `Number`: how integer literals are converted from
their token text, and how 64-bit values are represented. Both findings below are
faces of the same limitation — Orbit's integer `Number` is a **signed 64-bit**
value, and the string→Number conversions do not handle the top of the range.

Prefix: `NUM`.

---

## NUM-001 — Integer literals above INT64_MAX are silently clamped
**Severity:** Medium · **Status:** OPEN · **Location:** `orbit/orbiter/datatype/number.cpp:778` (`IntNew(string, base)`), `:794` (`UIntNew(string, base)`); reached from `orbit/liftoff/parser/parser.cpp:1719`

Integer literals are materialised by converting the token's digit string with
`std::strtol` / `std::strtoul`:

```cpp
HNumber IntNew(Isolate *isolate, const char *string, int base) {
    const auto num = std::strtol(string, nullptr, base);   // saturates on overflow
    return IntNew(isolate, num);
}
```

`strtol` **saturates** to `LONG_MAX` (and sets `errno = ERANGE`) when the value
overflows, and the code ignores `errno`. So a literal that exceeds the range is
silently turned into the wrong number instead of erroring:

- `0xFFFFFFFFFFFFFFFF` (should be `2^64-1`) parses as `9223372036854775807`
  (`INT64_MAX`), not as an unsigned max.
- Any decimal/hex/octal/binary literal above `INT64_MAX` clamps the same way.

Two aggravating factors:
- **Portability:** `strtol` returns `long`, which is **32-bit on Windows
  (LLP64)** — there the clamp happens at `2^31-1`, so even `0x80000000`
  mis-parses. `strtoll`/`strtoull` (`long long`) would at least be 64-bit
  everywhere.
- No `ERANGE` check means overflow is indistinguishable from a legitimate
  `INT64_MAX` literal.

**PoC (confirmed live against `bin/Orbit`):**
```orbit
import "io"
io.print(0xFFFFFFFFFFFFFFFF.str())   # prints 9223372036854775807, not 18446744073709551615
```

**Fix:** use the 64-bit `strtoll`/`strtoull`, check `errno == ERANGE` after the
call, and surface an out-of-range literal as a compile error (or, once a true
unsigned path exists, route large hex/`u`-suffixed literals through it — see
NUM-002). Related: the `U_NUMBER_HEX` path already calls `UIntNew`
(parser.cpp:1740), but `UIntNew` has the same no-ERANGE-check problem and still
lands in a signed 64-bit Number (NUM-002).

---

## NUM-002 — No true unsigned 64-bit: `read_u64` / `UIntNew` surface values ≥ 2^63 as negative
**Severity:** Medium · **Status:** OPEN · **Location:** `orbit/orbiter/datatype/number.cpp:794` (`UIntNew`); observable via `orbit/orbiter/datatype/rawptr.cpp` (`read_u64`)

Orbit's integer `Number` is signed 64-bit. `UIntNew` stores the full bit
pattern, but there is no representation for a positive value in the range
`2^63 .. 2^64-1`: such values are interpreted (and printed, compared, and used
in arithmetic) as their signed two's-complement equivalent.

`RawPtr.read_u64` documents its result as an "unsigned Int", but reading a value
with the top bit set yields a negative Number:

```orbit
let p = Rawptr.alloc(8, fill=255)   # buffer = 0xFFFFFFFFFFFFFFFF (SIZE_MAX)
io.print(p.read_u64().str())        # prints -1, not 18446744073709551615
```

Consequences:
- The `read_u64` docstring is misleading — the value is signed.
- Any native API returning a `size_t` / `uint64_t` in the top half of the range
  (e.g. `PCRE2_UNSET == SIZE_MAX`) cannot be compared against its documented
  unsigned constant; callers must know to test against `-1` / `< 0` instead
  (this is exactly the workaround used in `stdlib/regex/match.orb`).
- `read_u32`/`read_u16`/`read_u8` are unaffected (they fit in signed 64-bit);
  only the full-width `u64` (and `UIntNew` of large values) is lossy in
  interpretation.

**PoC (confirmed live):** the `alloc(8, fill=255)` snippet above; `read_u64()`
returns `-1`, and `read_u64() == -1` is `true`.

**Fix:** decide the intended semantics. Either (a) document `read_u64` as
returning a signed 64-bit value and drop the "unsigned" claim (cheap, honest),
or (b) introduce a real unsigned/bignum path so values ≥ 2^63 round-trip as
positive (larger change; would also unblock NUM-001's unsigned literals).
