# `number.md` — `orbit/orbiter/datatype/number.cpp` (integer Number: parsing & representation)

Findings for Orbit's integer `Number`: how integer literals are converted from
their token text, and how 64-bit values are represented. The findings below are
faces of the same limitation — Orbit's integer `Number` is a **signed 64-bit**
value, and the string→Number conversions do not handle the top of the range.

Prefix: `NUM`.

---

## NUM-005 — Comparing two small integers truncates their difference to 32 bits
**Severity:** High (silent wrong result in every comparison of large values) · **Status:** OPEN · **Location:** `orbit/orbiter/datatype/oops.cpp:373` (`Compare`)

```orbit
a := 4294967296
a > 0             # false
b := 2147483648
b > 0             # false
b < 0             # true
if a > 0 { ... }  # not taken
```

For two SMIs `Compare` computes the difference into `delta`, declared `auto delta
= 0`, an `int`, because `CompareFn` reports through an `int &`. Only the low 32 bits
survive: a difference with those bits clear reads as equal, one with bit 31 set as
negative. So the result of `<`, `<=`, `>`, `>=` is wrong whenever the two values
differ by 2^31 or more. Heap Numbers go through `NumberCompare`, which compares
values, and are right; equality does not use `delta`.

**Found** while verifying the NUM-004 fix: remainder checks using `<` failed on
remainders that were correct.

**PoC:** [`poc/number/num-005-compare-smi.orb`](poc/number/num-005-compare-smi.orb)
*(confirmed live: the controls pass, twelve checks fail)*.

**Fix (suggested, verified by probe 2026-11-05, then reverted):** compare instead of
subtracting:

```cpp
const auto l = (MSSize) O_FROM_SMI(left);
const auto r = (MSSize) O_FROM_SMI(right);

delta = l < r ? -1 : (l > r ? 1 : 0);
```

With the probe the PoC passes and the gates are unchanged.

---

## NUM-004 — `//` rounds differently on a heap Number than on a SMI
**Severity:** High (silent wrong result) · **Status:** FIXED (2026-11-05) · **Location:** `orbit/orbiter/datatype/number.cpp:142` (`NumberIDiv`)

**Fix verified (live: the PoC passes; it failed on the three heap quotients with
the correction in place; `//` and `%` of 400 operand pairs, SMI and heap on both
sides and every sign combination, match an oracle computed apart, except
`-2^61 // -1`, an overflow of the SMI path recorded in NUM-003; `ortest` and the
PoC gate are unchanged otherwise).** The correction towards -∞ is gone, so
`NumberIDiv` truncates like the SMI path and `NumberMod`, and
`a == (a // b) * b + a % b` holds on heap values:

```cpp
const auto n = IntNew(isolate, a / b);
```

<details><summary>Original report</summary>

Orbit's `//` truncates towards zero and `%` takes the sign of the dividend,
C-style (pinned by `ortest/operators/02_basic_ops.orb`), and the SMI fast
path does exactly that (`na / nb`, `na % nb`). `NumberIDiv`, which handles
any heap operand, adds a correction that rounds the quotient towards -∞
instead, while `NumberMod` still truncates:

```orbit
-7 // 2                       # -3 (SMI)
-9223372036854775807 // 2     # -4611686018427387904, truncation gives ...903
-9223372036854775807 % 2      # -1, truncating: consistent with -3, not with ...904
```

The same division gives a different quotient depending on the size of its
operands (beyond 2^61-1 in magnitude a value is a heap Number), and on heap
values `a == (a // b) * b + a % b` no longer holds.

```cpp
// C++ truncates towards zero; adjust towards -∞ when the signs differ and
// there is a non-zero remainder.
auto q = a / b;
const auto r = a % b;
if (r != 0 && ((r ^ b) < 0))
    q--;
```

**Found** while mapping the number code for unsigned 64-bit support
(NUM-002).

**PoC:** [`poc/number/num-004-idiv-heap.orb`](poc/number/num-004-idiv-heap.orb)
*(confirmed live: the two SMI controls and the two heap remainders pass, the
three heap quotient checks fail)*.

**Fix (suggested, not tried):** drop the correction, so `NumberIDiv` truncates
like the SMI path and `NumberMod`. Part of the NUM-002 work, where `//` and
`%` move to sign and magnitude anyway.

</details>

---

## NUM-003 — Integer overflow wraps silently; a product of two SMIs is not even checked
**Severity:** High (silent wrong result; undefined behavior in C++) · **Status:** OPEN · **Location:** `orbit/orbiter/datatype/oops.cpp:63` (`ObjectAddSubMul`, SMI fast path), `orbit/orbiter/datatype/number.cpp:73`, `:87`, `:101` (`NumberAdd`, `NumberSub`, `NumberMul`)

Integer arithmetic that leaves the 64-bit range gives a wrong number and no
error:

```orbit
2305843009213693951 * 8      # -8 (two SMIs; SMI max is 2^61-1)
4611686018427387903 * 4      # -4 (a heap Number)
9223372036854775807 + 1      # -9223372036854775808
9223372036854775807 * 2      # -2
-2305843009213693952 // -1   # -2305843009213693952: 2^61 does not fit a SMI
(-9223372036854775807 - 1) // -1   # INT64_MIN: 2^63 does not fit an I64
```

- The SMI fast path computes `+`, `-` and `*` on `MSize` and hands the result
  to `IntNew`. Two SMIs (62-bit values, `kSMIBit`) cannot overflow 64 bits
  when added or subtracted, but their product can, and nothing checks it:
  the product wraps modulo 2^64 before `IntNew` sees it.
- On a heap Number, `NumberAdd`/`NumberSub`/`NumberMul` compute `a + b`,
  `a - b`, `a * b` on `I64`: signed overflow is undefined behavior in C++,
  and in practice wraps.
- Division has the same overflow at one point: the SMI path stores `na / nb` with
  `O_TO_SMI` unchecked (`oops.cpp:141`), so `-2^61 // -1` wraps; `NumberIDiv`
  computes `INT64_MIN / -1`, and `NumberMod` `INT64_MIN % -1`, undefined behavior
  in C++ that gives INT64_MIN on arm64 and is a SIGFPE on x86.

**Found** while mapping the number code for unsigned 64-bit support
(NUM-002).

**PoC:** [`poc/number/num-003-overflow.orb`](poc/number/num-003-overflow.orb)
*(confirmed live: ten of the eleven checks fail; `INT64_MIN % -1` happens to give 0 on arm64)*. Its expectations follow the
design chosen for NUM-002 — a heap Number holds a sign and a 64-bit
magnitude, so results up to 2^64-1 in magnitude are exact and anything beyond
is an error.

**Fix (suggested, not tried):** detect overflow instead of wrapping:
`__builtin_mul_overflow` on the SMI product, falling back to the heap path;
heap arithmetic on sign and magnitude with `__builtin_add_overflow` /
`__builtin_mul_overflow` on `U64`, and an error past `2^64-1` in magnitude.
Part of the NUM-002 work.

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

**PoC:** [`poc/number/num-002-read-u64.orb`](poc/number/num-002-read-u64.orb)
*(confirmed live: `read_u64()` of eight 0xFF bytes returns `-1`, and
`read_u64() == -1` is `true`)*.

**Fix:** decide the intended semantics. Either (a) document `read_u64` as
returning a signed 64-bit value and drop the "unsigned" claim (cheap, honest),
or (b) introduce a real unsigned/bignum path so values ≥ 2^63 round-trip as
positive (larger change; would also unblock NUM-001's unsigned literals).

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

**PoC:** [`poc/number/num-001-literal-clamp.orb`](poc/number/num-001-literal-clamp.orb)
*(confirmed live: five of six checks fail)*. The original snippet:
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
