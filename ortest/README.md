# `ortest/` — acceptance & regression suites

Reusable `.orb` feature tests, grouped by topic, run on every build of the
interpreter — especially before a production build. `ortest/run.sh` is the gate:
**all suites must be green to ship.**

```sh
# rebuild the compiler first, then:
ortest/run.sh              # run every topic
ortest/run.sh regalloc     # run a single topic
```

The runner decides pass/fail from each suite's exit code and output, so it is
robust to bytecode-dump noise on stdout. Exit 0 = all green.

## `ortest/` vs `issues/poc/`

Two directories, two roles — keep them distinct:

- **`issues/poc/`** — bug *reproducers*, one file per tracked finding ID
  (`ir/ir-003-…`). Answers *"did a known bug come back?"*. Indexed by
  `issues/*.md`; governed by [`issues/GUIDE.md`](../issues/GUIDE.md).
- **`ortest/`** — broad *feature / acceptance* suites, grouped by subsystem, not
  tied to any bug. Answers *"does feature X still work?"*.

A tiered feature suite does **not** belong under `poc/` (that space is per-ID).
When you fix a bug, its reproducer goes in `poc/`; the general behavior it
touches is exercised here.

## Layout

```
ortest/
  run.sh              # central runner (this is the release gate)
  <topic>/            # one folder per subsystem
    NN_name.orb       # a self-contained suite
```

Current topics: `regalloc/` (register allocator, tiers `01_variables` …
`05_pressure`, rising in difficulty), `oop/` (class hooks, inheritance, instance
layout, trait C3/MRO), `typehooks/` (str/eq user hooks), `operators/` (language operators, e.g.
`01_is` for the `is` type test). Add a new subsystem as a new subfolder — the
runner picks it up automatically.

## Writing a suite

Each `.orb` is standalone and declares its expectation in an `# EXPECT:` line
(same directives as `issues/poc/`):

| Directive | Passes when |
|---|---|
| `# EXPECT: ok` | compiles and runs cleanly (exit 0) |
| `# EXPECT: error` | fails (exit ≠ 0) |
| `# EXPECT: error: <substr>` | fails **and** output contains `<substr>` |
| `# EXPECT: contains: <substr>` | runs (exit 0) **and** output contains `<substr>` |

The usual form for a behavioral suite is `# EXPECT: contains: ALL TESTS PASSED`:
run a battery of assertions, then print the marker only when every one passed.
The house skeleton (see any existing suite):

```orb
# <one-paragraph description of what the suite covers and why>
#
# EXPECT: contains: ALL TESTS PASSED

import "io"

var fails = 0

func check(name, got, expected) {
    if got == expected {
        io.print("OK   ", name)
    } else {
        io.print("FAIL ", name, "-- got:", got, "expected:", expected)
        fails += 1
    }
}

# ... definitions and check(...) calls, each with a known-good oracle ...

if fails == 0 {
    io.print("ALL TESTS PASSED")
} else {
    io.print(fails, "TEST(S) FAILED")
}
```

Conventions:

- **English comments**, a header paragraph stating what the suite covers.
- Assert against **known-good oracles** (compute the expected value by hand),
  not against the interpreter's current output.
- A suite that encodes a fixed bug should note it (which finding, what shape),
  so a future regression is legible — e.g. `regalloc/04_crosscall_results.orb`.
- Mind the [language gotchas in `AGENTS.md`](../AGENTS.md#language-gotchas-when-writing-orb-test-programs):
  `:=` (not `let`) inside functions, reserved type keywords (`u8`, `i32`, `f64`,
  … cannot be identifiers), one statement per line.
