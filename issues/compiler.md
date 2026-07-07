# compiler — bug report

**Component:** `orbit/liftoff/compiler.cpp` (compile driver: parse → IR → codegen) · **ID prefix:** `COMP`
**PoCs:** none yet · **Last reviewed:** 2026-07-07
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## COMP-001 — Any syntax error in a script asserts instead of reporting (release: UB)
**Severity:** High (every user-facing syntax error crashes the binary) · **Status:** OPEN · **Location:** `orbit/liftoff/compiler.cpp:120` (`Compiler::Compile(const char *, Scanner &)`)

**Reproducer (any parse error in file mode):**

```orb
x := ]
```

```
Invalid syntax: unexpected token or expression
Assertion failed: (false), function Compile, file compiler.cpp, line 120.
[exit 134]
```

Also reachable with everyday typos, e.g. a one-line class body missing the
separator: `class A { pub var name }` (the syntax error itself is legitimate —
`class A { pub var name; }` parses fine — but the reporting path crashes).
Interactive mode is unaffected: `eval` reports the same errors gracefully, so
this only bites `./bin/Orbit script.orb`.

**Root cause:** the parse-failure branch is a placeholder, not error
propagation:

```cpp
auto ast = parser.Parse();
if (!ast) {
    auto error = parser.GetLastError();
    printf("%s\n", error.message);
    assert(false);            // <- debug: abort; release (NDEBUG): falls through!
}

IRBuilder builder(this->isolate_, this->is_module_);
const auto ir = builder.Generate(ast);   // release: ast is empty here -> UB
if (!ir)
    assert(false);                        // same pattern for IR failures
```

Two defects in one:

1. **Debug builds:** the process aborts (SIGABRT) after printing the message —
   no filename/line, exit code 134 instead of a clean failure.
2. **Release builds (`NDEBUG`):** `assert` compiles out, so execution
   *continues past the failed parse* and `builder.Generate` runs on an empty
   AST handle — undefined behavior / crash. The `!ir` branch has the same
   fall-through.

**Suggested fix:** propagate instead of asserting — on `!ast` (and `!ir`)
set/keep the error on the isolate and `return {}`, letting the driver print a
proper `file:line: message` diagnostic and exit non-zero (the same surface the
interactive `eval` path already presents).
