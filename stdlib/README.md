# `stdlib/` — Orbit standard library

The **Orbit-side standard library**: the `.orb` modules shipped with the
language. This is the idiomatic surface a user touches with `import "io"`,
`import "error"`, … — not the engine primitives the modules sit on (those live
in `orbit/orbiter/` and are reached as `::orbit::*` builtins).

> **Status:** early and growing. A handful of modules are usable today; several
> listed here are still planned. Each entry below is marked, and work-in-progress
> modules call out their current limitations. Bytecode and some APIs are not
> stable yet — expect changes.

## Running code against the stdlib

The interpreter finds these modules via the `ORBIT_PATH` environment variable
(`:`-separated, like `PATH`). Point it at this directory:

```sh
ORBIT_PATH="$PWD/stdlib" ./bin/Orbit your_script.orb
```

```orbit
import "io"

io.print(b"hello, world")        # -> hello, world\n
name := io.input(prompt=b"name? ")
io.print(b"hi,", name)
```

## Module catalog

Status legend: ✅ available · 🚧 work in progress (usable, with caveats) ·
📋 planned (not present yet).

### Available

| Module | Import | Backing | Notes |
|---|---|---|---|
| **io** | `import "io"` | `::orbit::io` builtin + pure Orbit | Standard streams as `File` objects (`stdin`/`stdout`/`stderr`), `print`/`perror`/`input`, `open`, buffered reader/writer, and the core IO traits. The most complete module. |
| **error** | `import "error"` | pure Orbit | Ready-made error constructors aligned with the kinds the engine raises (`ValueError`, `TypeError`, `OSError`, `IndexError`, `KeyError`, …). Each is a partial application of `Error.create(@Kind)`; call with a reason to build one: `panic ValueError("count must be positive")`. |
| **runtime** | `import "runtime"` | `::orbit::runtime` builtin | Environment introspection: `os`, `executable`, `get_argv()`, `get_config()`, and the engine `version` (plus parsed `version_major`/`minor`/`patch`). |

### Work in progress

| Module | Import | Status | Caveats |
|---|---|---|---|
| **readline** | `import "readline"` | 🚧 | Line editing + history via the **system** readline-compatible library (GNU readline, or libedit on macOS/BSD), selected at load time by a `when runtime.os` block. **POSIX/macOS only — no Windows branch.** Depends on the FFI layer (`native func … from "lib…"`), which is itself POSIX-complete / Windows-partial. Surface is intentionally thin: `read(prompt)` + `add(line)` + auto-history; no completion or custom key-binding API, and duplicate history entries are not filtered. |
| **repl** | `import "repl"` | 🚧 | Interactive read-eval-print loop (`repl.default_session.run()`; the interpreter's interactive mode runs this). Built on `readline` (inherits its platform limits) and on the engine's `eval` + `Context` + `trap`/`await`. Current limitations: **single-line input only** — the `ps2` continuation prompt exists but multi-line entry isn't wired yet; the session ends when a line merely *contains* the substring `"exit"` (naive check); errors are trapped and printed so the loop survives. |

### Planned

Not present yet — listed so contributors know the intended shape. The names and
scope mirror the modules already in the works (same surface, implemented the
Orbit way):

| Group | Modules |
|---|---|
| Text & data | `regex` (Perl-like regex) · `json` (encode/decode) · `ini` (INI parser) · `base64` (Base16/32/64 encodings) · `enum` (algorithms over enumerables) |
| OS & processes | `ospath` (pathname manipulation) · `subprocess` (spawn / manage processes) |
| Numeric | `random` (pseudo-random numbers) |
| Concurrency | `syncutil` (synchronization primitives) |
| Cryptography | `hashlib` (secure hashes / digests) · `ssl` (TLS over sockets) |
| Compression & archives | `zlib` · `bz2` · `lzma` · `zipfile` |
| Networking | `http` (client/server) · `url` (URL handling) |
| Tooling | `argparse` (CLI option parsing) · `unittest` (unit-testing framework) |

## Layout

**Flat by default; package only when a module outgrows one file.**

- One top-level entry = one `.orb` file **or** one directory-as-package.
- A package's entry file has the **same name as its directory** (`io/io.orb`) —
  `import "io"` resolves to it. The entry file re-exports the package's public
  surface.
- Submodules are reached by path: `import Readable from "io/traits"`,
  `import BufferedWriter from "io/bufio/writer"`. They are implementation
  detail — prefer importing the package entry (`import "io"`) from user code.
- A module stays *small* by default; split into a package once it grows clear
  internal sub-areas (as `io` did).

## The three layers

A module combines up to three building blocks; pick the **lightest** that does
the job.

| Layer | Where it lives | Use when … |
|---|---|---|
| **C++ builtin** (`::orbit::*`) | `orbit/orbiter/…` (engine) | the operation needs deep VM/GC/type machinery (e.g. raw IO syscalls, runtime introspection) |
| **Native binding** (`native func`) | inside the `.orb` module | the operation maps cleanly to a libc/OS symbol (e.g. `readline`, `add_history`) |
| **Pure Orbit** | inside the `.orb` module | composition over the above, or no foreign call at all (e.g. error constructors, iterator helpers) |

```orbit
# readline.orb — native binding hidden behind a pure-Orbit class
when runtime.os == "darwin" {
    native func readline(prompt: ptr): ptr from "libedit"
} else {
    native func readline(prompt: ptr): ptr from "libreadline"
}

pub class Readline {
    pub func read(prompt="") {
        line_ptr := readline(prompt)        # raw native call
        
        if line_ptr.is_null() { return nil }
        defer line_ptr.free()

        line := line_ptr.read_string()      # wrapped into an Orbit String

        return line
    }
}
```

**Rule of thumb:** don't add a `::orbit::*` builtin unless the operation can't be
expressed with `native func` + pure Orbit at acceptable cost. Growing the stdlib
is far cheaper than growing the engine surface.

## Conventions

### Naming
- **`snake_case`** for functions, parameters, module-level variables.
- **`PascalCase`** for types/classes/traits and error kinds.
- **`SCREAMING_SNAKE_CASE`** for constants (`O_READ`, `SEEK_SET`, `BUFFER_SIZE`).
- Private helpers are simply not marked `pub`.

### Visibility
- **`pub`** marks the public API; everything else is private to the module.
- A package's entry file decides the package surface via `pub` (re-)exports;
  submodules are private by construction.

### Composition with traits
The `io` subsystem is organised around small traits (`Closable`, `Readable`,
`LineReadable`, `Writeable` in `io/traits.orb`). Concrete types declare what they
honor (`class File impl Closable + Readable + Writeable`), and generic code
(e.g. `BufferedReader`) targets the trait, not the concrete type. New IO-shaped
resources (sockets, pipes, in-memory buffers) should implement the relevant
traits to interoperate.

### Error handling
- **Default: `panic`.** Idiomatic and wired through the VM already;
  most functions should raise on failure rather than return a status.
- **`Result`-style when failure is normal flow** — parsing user input, probing
  for existence, retry-in-a-loop attempts. Don't use it for rare/exceptional
  conditions; that's what panicking is for.
- Pick **one style per public feature** and stick to it.
- Raise **specific kinds** from `error.orb` (with a clear `kind` atom), not bare
  strings — so user code and engine code can be caught symmetrically.

### Cross-OS constants (flags, modes, …)
When a module exposes integer flags that differ between POSIX and Windows
(`open` flags, …):
- **Define stable Orbit-side values** in the `.orb` module (clean bitmask powers
  of two), independent of any OS header.
- The backing `::orbit::*` builtin **translates** them to the platform's real
  bit pattern at the call boundary.

This decouples the Orbit ABI from libc/Win32 header drift. Constants that are
historically identical across platforms (`SEEK_SET=0`, `SEEK_CUR=1`,
`SEEK_END=2`) may be reused as-is, documented as a stability guarantee.

### Documentation
Document every `pub` symbol with the doc-comment format in
[`../docs/documentation-guide.md`](../docs/documentation-guide.md):

- **`/*! … */`** at the **top of the file** documents the module.
- **`/** … */`** before a `func`/`class`/`trait`/`native` declaration documents
  it (works through `pub`/`prot`/`@[decorator]` prefixes too).
- Same `@brief` / `@param` / `@return` / `@panic` / `@see` / `@example` tags as
  the C++ runtime methods, written in English. `@brief` is the first line and a
  single sentence; `@panic` lists each error kind that can be raised; `@example`
  covers the happy path plus an edge case for non-trivial functions.

The existing modules (`io/io.orb`, `error.orb`, `readline.orb`, …) are the
working style reference.

## Adding a new module — checklist

1. **Pick the layer mix.** Pure Orbit + `native func` if possible; justify any
   new `::orbit::*` builtin in the module header.
2. **Name it** lowercase, short, ideally one word (`io`, `json`, `regex`). No
   `_` in the module name itself.
3. **Start as a single `.orb` file.** Promote to a package only when it grows
   clear internal sub-areas.
4. **Public surface only via `pub`;** everything else private.
5. **Docstring every `pub`** per the documentation guide (module `/*! */` +
   per-declaration `/** */`).
6. **Consistent error style** across the module (raise *or* Result per feature).
7. **No leaked native types** — wrap raw pointers / native ints behind ergonomic
   Orbit values (see how `readline` hides `ptr`).
8. **Update this README's catalog** with the right status marker.

## Roadmap

Ordered by enablement (what unblocks what):

```
DONE  io / error / runtime          (usable today)
WIP   readline / repl               (POSIX/macOS; depend on FFI + eval/Context)
─────────────────────────────────────────────────────────────────
Pure-Orbit first (need only the language + import pipeline):
      enum · ospath · json · base64 · url
Native-backed (need FFI / engine support):
      random · regex · hashlib · subprocess · syncutil
      compression: zlib · bz2 · lzma · zipfile
      ssl · http
Tooling:
      argparse · unittest
```

The pure-Orbit modules need nothing beyond the existing language types and the
import pipeline, so they can be drafted in parallel. The native-backed ones land
as the FFI surface and any required engine support fill in. WIP modules graduate
to ✅ once their platform coverage and the engine features they lean on (FFI on
Windows, `eval`/`Context` for the REPL) are solid.
