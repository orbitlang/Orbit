# Contributing to Orbit

Thanks for your interest in contributing to Orbit. The language is in early
alpha and under active development; the contribution process is intentionally
lightweight, and a few conventions keep the codebase coherent as it grows.

> If you are an **AI agent** (or driving one), read this file *and*
> [`AGENTS.md`](AGENTS.md). `AGENTS.md` contains the operational rules an agent
> needs; this file covers the human-facing process.

## Ground rules

- **Open an issue before a large change.** For anything beyond a small fix,
  describe the problem and the intended approach first. The architecture is
  still moving and a quick conversation saves rework.
- **Keep changes focused.** One logical change per pull request. If you spot an
  unrelated issue while working, note it separately rather than bundling it in.
- **Don't break the build.** Every PR must compile cleanly and, where relevant,
  pass the test suite.
- **Match the surrounding style.** Read the neighbouring code and mirror its
  naming, comment density, and idioms rather than importing a different style.

## Project structure

See the [repository layout](README.md#repository-layout) in the README. The
three pillars are:

- **Liftoff** (`orbit/liftoff/`) — the compiler.
- **Orbiter** (`orbit/orbiter/`) — the runtime and VM.
- **Stratum** (`lib/stratum/`) — the vendored allocator.

## Building & testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The `Orbit` binary lands in `bin/`. To run a script against the bundled
standard library:

```sh
ORBIT_PATH="$PWD/stdlib" ./bin/Orbit your_script.orb
```

The C++ test target under [`test/`](test/) is **not currently maintained** — it
is out of date and effectively unusable for now; restoring it is planned future
work, so don't rely on it.

When you change the **compiler IR or register allocator**, run the bytecode
regression script and confirm it still prints `ALL TESTS PASSED`:

```sh
ORBIT_PATH="$PWD/stdlib" ./bin/Orbit issues/poc/ir/phi-regalloc.orb
```

That script is part of the in-tree regression suite under
[`issues/poc/`](issues/poc/) (see "Tracking issues" below); after a rebuild you
can re-run every reproducer at once with `issues/poc/run.sh`.

## Coding conventions

### C++ (engine code)

- **C++17** (MSVC builds in C++20). No new third-party runtime dependencies.
- Follow the **Google C++ Style Guide** for naming and layout, and keep the
  existing file header (`// This source file is part of the Orbit project. //
  Licensed under the Apache License v2.0`).
- Memory goes through the runtime's allocators (`IsolateAllocator`,
  `MakeObject`, GC tracking) — never raw `new`/`delete` for managed objects.
- Document native code with **Doxygen**-format comments; runtime-method
  docstrings follow the conventions in [`docs/documentation-guide.md`](docs/documentation-guide.md).
- Prefer `assert`-backed invariants in hot paths; surface real errors through
  the panic/error machinery, not `assert(false)` in user-reachable paths.

### Orbit (`.orb` standard library)

- Follow [`stdlib/README.md`](stdlib/README.md): pick the lightest layer
  (pure Orbit > `native func` > `::orbit::*` builtin), and keep the public
  surface small.
- `snake_case` for functions/variables, `PascalCase` for types/error kinds,
  `SCREAMING_SNAKE_CASE` for constants.
- Docstring every `pub` symbol.

### Commits

This repo uses the **Angular commit convention**:

```
<type>(<scope>): <summary>

type  = fix | feat | refactor | docs | test | chore | perf
scope = scanner | parser | ir | linearscan | vm | gc | runtime | stdlib | …
```

Examples from history:

```
fix(scanner): enhance buffer management and capacity handling
feat(vm): add MOV opcode for phi edge copies
```

Keep the summary in the imperative mood and under ~72 characters.

## Pull requests

1. Branch off `main`.
2. Make your change; keep it focused and compiling.
3. Update docs/tests affected by the change.
4. Open the PR with a clear description of *what* and *why*. Link the issue if
   there is one.

## Reporting bugs

Open an issue with:

- What you ran (the `.orb` snippet or command, ideally minimal).
- What you expected vs. what happened.
- Platform, compiler, and build type.

## Tracking issues (`issues/`)

The repo carries an in-tree issue tracker at [`issues/`](issues/): bugs,
defects, and known issues are recorded **component by component** (one Markdown
file per component), each with a stable ID, severity, and a status that moves
`OPEN → PARTIAL → FIXED` (or `WONTFIX`). Fixed findings keep their original
analysis archived, and most carry a runnable reproducer under `issues/poc/` so
the whole set doubles as a fast regression suite (`issues/poc/run.sh`).

This is optional but encouraged: it's a good home for a defect you found but
aren't fixing yet, the root-cause notes behind a fix, or a regression test for a
bug you just closed. The format is small and deliberately
[multi-agent friendly](AGENTS.md). **Before adding or updating an entry, read
[`issues/GUIDE.md`](issues/GUIDE.md)** — it is the single source of truth for the
naming, status, verification, and PoC conventions, with copy-paste templates.

## License

By contributing, you agree that your contributions are licensed under the
project's [Apache License 2.0](LICENSE).
