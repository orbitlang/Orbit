# AGENTS.md

Operational guide for AI coding agents working on Orbit. It follows the
[agents.md](https://agents.md) convention and is read by Claude Code, Cursor,
and other agent tooling. Human contributors should read
[`CONTRIBUTING.md`](CONTRIBUTING.md); the two overlap intentionally.

Orbit supports AI-assisted development as part of its workflow. Agents may help,
but they must follow the directives in this file scrupulously, and a human must
review the work before any pull request is opened. The rules below encode
hard-won gotchas, not bureaucracy.

## The 30-second orientation

Orbit is a programming language implemented in C++17. Three components:

- **Liftoff** — `orbit/liftoff/` — the compiler (scanner → parser → IR →
  register allocation → bytecode).
- **Orbiter** — `orbit/orbiter/` — the runtime: register VM (`vm.cpp`),
  instruction set (`opcode.h`), GC (`memory/`), fibers, object model
  (`datatype/`), FFI (`native/`).
- **Stratum** — `lib/stratum/` — vendored memory allocator. **Do not edit
  unless explicitly asked**; it is an upstream dependency.

The grammar of the language is the source of truth for syntax:
[`orbit/liftoff/grammar.ebnf`](orbit/liftoff/grammar.ebnf).

## Build & run

Standard path:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Output goes to `bin/`. **If `cmake`/`ninja` are not on `PATH`** (common in
sandboxed or IDE-managed environments), invoke the binaries shipped with your
toolchain by their full path, and build the configured directory directly —
for example:

```sh
<path-to>/ninja -C <build-dir>
```

If the project has already been configured (an IDE often creates its own build
directory), reuse that directory rather than reconfiguring from scratch.

Run a script (the stdlib needs `ORBIT_PATH`):

```sh
ORBIT_PATH="$PWD/stdlib" ./bin/Orbit script.orb
```

## Language gotchas (when writing `.orb` test programs)

These will bite you if you write Orbit code from intuition:

- **`let` is only valid at module/class/trait scope.** Inside a function body
  use the short declaration `v := expr`. Using `let` in a function aborts the
  compile.
- **`io.print` consumes bytes.** Prefer `b"..."` byte literals for printed
  output in test scripts; mixing string and bytes paths can surprise you.
- Operators that exist: ternary `a ? b : c`, elvis `a ?: b`, nil-coalescing
  `a ?? b`, nil-safe selector `a?.b`, channels `ch <- v` / `<- ch`.
- Atoms are `@Name` (interned); error kinds are atoms.
- `new` constructs class instances: `new Person(b"x")`.

A ready-made register-allocation regression suite lives at
[`issues/poc/ir/phi-regalloc.orb`](issues/poc/ir/phi-regalloc.orb). After
**any** change to the IR, the register allocator, or VM stack discipline, run it
and confirm `ALL TESTS PASSED`:

```sh
ORBIT_PATH="$PWD/stdlib" ./bin/Orbit issues/poc/ir/phi-regalloc.orb
```

It is part of the `issues/poc/` regression suite — `issues/poc/run.sh` runs it (and
all other proof-of-concept reproducers) and verifies the marker for you.

## Where things live

| You want to change… | Look in |
|---|---|
| Lexing | `orbit/liftoff/scanner/` |
| Parsing / AST | `orbit/liftoff/parser/` |
| IR, register allocation, codegen | `orbit/liftoff/ir/` |
| Symbol resolution | `orbit/liftoff/symtable.{h,cpp}` |
| The instruction set | `orbit/orbiter/opcode.h` |
| The interpreter loop | `orbit/orbiter/vm.cpp` |
| Built-in object types | `orbit/orbiter/datatype/` |
| Garbage collector | `orbit/orbiter/memory/` |
| FFI | `orbit/orbiter/native/` |
| Standard library (`.orb`) | `stdlib/` |

## Conventions you must follow

- **C++17**, no new third-party runtime dependencies.
- Every source file starts with the project header
  (`// This source file is part of the Orbit project. // Licensed under the
  Apache License v2.0`).
- Managed memory goes through the runtime allocators / GC tracking
  (`MakeObject`, `IsolateAllocator`, `O_GC_TRACK_RETURN`, …) — never raw
  `new`/`delete` for managed objects.
- Follow the **Google C++ Style Guide** for naming and layout.
- Document native code with **Doxygen**-format comments; runtime-method
  docstrings follow [`docs/documentation-guide.md`](docs/documentation-guide.md). All docs in **English**.
- **Angular commit convention** for commit messages (see `CONTRIBUTING.md`).
- Adding an opcode: append it at the **tail** of the `OPCode` enum in
  `opcode.h` to avoid renumbering existing bytecode, add its string to
  `OPCODE_STRINGS`, implement the `TARGET_OP` case in `vm.cpp`, and wire
  codegen in `orbit/liftoff/codegen.cpp`.

## When the instruction encoding matters

Opcode bit-field layouts are documented inline next to each entry in
`opcode.h`. The VM decoder (`vm.cpp`) and the codegen emitter
(`codegen.cpp`) must agree on field widths — a mismatch is a silent
miscompile, not a crash. When touching jumps/encodings, verify both sides and
re-run `phi-regalloc.orb` (via `issues/poc/run.sh`).

## Working norms for agents

- **Verify, don't assume.** Read the actual code before asserting how it
  behaves; the architecture moves faster than any summary.
- **Report faithfully.** If a build or test fails, say so with the output. Do
  not claim something works that you have not run.
- **Stay in scope.** Make the requested change; flag unrelated issues
  separately rather than fixing them inline.
- **Don't commit or push unless asked.** Branch off `main` if you do.
- **Confirm before destructive or hard-to-reverse actions** (deletes,
  history rewrites, anything outward-facing).
- **Update docs/tests** affected by your change in the same pass.

## Tracked state & known issues

- **Project issue tracker: [`issues/`](issues/).** Bugs, defects, and known
  issues are tracked component-by-component there, each with a stable ID,
  severity, and status (OPEN / PARTIAL / FIXED / WONTFIX), plus runnable
  regression PoCs under [`issues/poc/`](issues/poc/). **Read
  [`issues/GUIDE.md`](issues/GUIDE.md) before filing or updating an issue** — it
  is the single source of truth for the format and workflow (built to be
  multi-agent safe). When you find or fix a defect, record it there in the same
  pass.
- Stdlib design & layering: [`stdlib/README.md`](stdlib/README.md).
