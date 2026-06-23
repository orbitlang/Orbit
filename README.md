<div align="center">

# Orbit

**A modern, concurrent programming language with a register-based virtual machine.**

[![status: alpha](https://img.shields.io/badge/status-alpha-orange)](#project-status)
[![version: 0.1.0](https://img.shields.io/badge/version-0.1.0-blue)](orbit/orbiter/version.h)
[![language: C++17](https://img.shields.io/badge/language-C%2B%2B17-00599C?logo=cplusplus)](CMakeLists.txt)
[![license: Apache 2.0](https://img.shields.io/badge/license-Apache--2.0-green)](LICENSE)
[![platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Linux%20%7C%20Windows-lightgrey)](#building-from-source)

</div>

---

> ⚠️ **Orbit is in early alpha (0.1.0).** The language compiles and runs, the
> core runtime is functional, and the standard library is taking shape — but
> syntax, bytecode, and APIs are still moving. It is not yet ready for
> production use. See [Project status](#project-status).

## What is Orbit?

Orbit is a general-purpose, dynamically-typed language with a focus on
**lightweight concurrency** and a **clean, expressive surface**. Programs are
compiled to bytecode and executed on **Orbiter**, a register-based virtual
machine with a work-stealing scheduler that runs Orbit fibers across multiple
OS threads.

It aims to feel familiar and comfortable to write, while keeping concurrency a
first-class part of the language rather than something added on the side. The
goal is a surface that stays explicit and readable, with a consistent object
model underneath.

The whole stack is built from scratch and has no third-party runtime
dependencies. That keeps it small and self-contained — a place to understand
how a dynamic language fits together end to end, and a comfortable base to keep
experimenting on.

The toolchain is written in C++17 and split into three components:

| Component | Role | Location |
|---|---|---|
| **Liftoff** | The compiler | [`orbit/liftoff/`](orbit/liftoff/) |
| **Orbiter** | The virtual machine | [`orbit/orbiter/`](orbit/orbiter/) |
| **Stratum** | The memory manager | [`lib/stratum/`](lib/stratum/) |

## A taste of the language

```orbit
import "io"

# Traits describe capabilities; classes implement them.
trait Greeter {
    pub func greet()
}

class Person impl Greeter {
    pub var name

    pub init(name) {
        self.name = name
    }

    pub func greet() {
        return "Hello, " + self.name + "!"
    }
}

func main() {
    p := new Person("Orbit")
    
    io.print(p.greet())
}

main()
```

### Language features

Working today on the VM and compiler:

- **Object model** — classes with single inheritance, traits with **C3
  linearization**, `init`/`cleanup`, `pub`/`prot` visibility, `const` methods,
  `weak` references.
- **First-class functions** — closures, currying, inline/anonymous functions,
  default & keyword & variadic parameters, decorators.
- **Concurrency** — `spawn` fibers, `async`/`await` with futures, typed
  **channels** (`<-`), and a work-stealing multi-threaded scheduler.
- **Generators & iterators** — `yield`, `for … in`, custom iterables.
- **Control flow** — `if`/`elif`/`else`, `switch` with `fallthrough`, labeled
  `loop`/`for` with `break`/`continue`, `defer`.
- **Error handling** — `panic`, `try`/`catch`/`finally`, `trap`, `Result`,
  typed error kinds via atoms (`@IOError`).
- **Resource scoping** — `sync` blocks for deterministic acquire/release.
- **Rich literals** — ints (dec/hex/oct/bin, signed & unsigned), floats,
  chars, strings, **raw** (`r"…"`) and **bytes** (`b"…"`) strings, atoms
  (`@name`), lists, tuples, sets, dicts.
- **FFI** — call C/libc symbols directly with `native func` (POSIX complete,
  Windows partial), with an FFI module cache.
- **Modules** — `import` with aliasing and selective imports, a layered
  standard library, and a search path (`ORBIT_PATH`).

The full grammar lives in [`orbit/liftoff/grammar.ebnf`](orbit/liftoff/grammar.ebnf).

## Building from source

Orbit vendors its only dependency (Stratum) under [`lib/`](lib/), so there is
nothing else to fetch.

### Platform support

| Platform / Architecture | x86 | x86_64 | ARM | Apple silicon |
|---|-----|---|---|---------------|
| Windows | ?   | ✓ | ? | NA            |
| Linux | ?   | ✓ | ✓ | NA            |
| macOS | NA  | ✓ | NA | ✓             |

`✓` tested · `?` untested · `NA` not applicable.

### Building on Unix-like systems

Requirements:

- A C++17 compiler (Clang or GCC)
- [CMake](https://cmake.org/) ≥ 3.15
- A build backend (Ninja or Make)
- POSIX threads (`-pthread`, linked automatically)

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The `Orbit` executable and the `Orbiter` shared library are written to
[`bin/`](bin/) (or `bin/Release/` for release builds); public headers are
exported to `bin/include/Orbit/`.

### Building on Windows

Building on Windows requires Visual Studio or the Microsoft Build Tools (MSVC
builds in C++20 mode). Either open the project directly in Visual Studio
(*File ▸ Open ▸ CMake…*, pick `CMakeLists.txt`, set `Orbit.exe` as the startup
item and build), or run the CMake commands above from a *Developer Command
Prompt*.

### Run

```sh
# Run a script
./bin/Orbit path/to/script.orb

# Evaluate an inline program
./bin/Orbit -c 'import "io"; io.print(b"hi")'

# Help and version
./bin/Orbit --help
./bin/Orbit --version
```

For now, point `ORBIT_PATH` at the bundled standard library so `import "io"`
and friends resolve:

```sh
ORBIT_PATH="$PWD/stdlib" ./bin/Orbit script.orb
```

> This step is temporary: the build will eventually copy `stdlib/` next to the
> executable, and `ORBIT_PATH` will no longer be needed for the bundled modules.

## Project status

Orbit is an **actively developed alpha**.

| Area | State |
|---|---|
| Scanner / parser / IR / codegen | ✅ Functional |
| Register VM & object model | ✅ Functional |
| Garbage collector | ✅ Functional |
| Fiber scheduler & concurrency | ✅ Functional |
| Classes, traits, generators, async, channels | ✅ Functional |
| FFI (`native func`) | 🚧 POSIX complete, Windows partial |
| Standard library | 🚧 Early — `io` taking shape, most modules planned |
| Static type annotations | 🚧 Parsed, not yet enforced |
| Bytecode stability | ❌ Not stable — expect breaking changes |
| Tooling (formatter, LSP, debugger) | ❌ Not started |

For more on the standard library — how the `.orb` modules are layered over the
engine primitives — see [`stdlib/`](stdlib/).

## Repository layout

```
orbit/
├── orbit/
│   ├── liftoff/          # Compiler (Liftoff)
│   │   ├── scanner/      #   lexer
│   │   ├── parser/       #   parser + AST
│   │   ├── ir/           #   SSA-style IR, register allocation, codegen
│   │   └── grammar.ebnf  #   language grammar (ISO-14977 EBNF)
│   ├── orbiter/          # Runtime (Orbiter)
│   │   ├── datatype/     #   built-in object types
│   │   ├── memory/       #   garbage collector
│   │   ├── module/       #   built-in modules
│   │   ├── native/       #   FFI
│   │   ├── vm.cpp        #   the bytecode interpreter
│   │   └── opcode.h      #   the instruction set
│   └── main.cpp          # CLI entry point
├── stdlib/               # Orbit-side standard library (.orb modules)
├── lib/stratum/          # Vendored memory allocator
├── docs/                 # Project documentation (guides for everyone)
├── issues/               # In-tree issue tracker + regression PoC suite
└── test/                 # C++ test suite
```

## Documentation

Proper, dedicated documentation is coming. In the meantime, the most reliable
references are the in-source documentation — the docstrings on runtime methods
and the stdlib `.orb` modules — and, for the language itself, the formal
grammar:

- [`orbit/liftoff/grammar.ebnf`](orbit/liftoff/grammar.ebnf) — the canonical language grammar
- [`docs/`](docs/) — project guides (e.g. how to write documentation)
- [`stdlib/`](stdlib/) — the standard-library modules and their docs
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — how to contribute
- [`AGENTS.md`](AGENTS.md) — guidance for AI coding agents

## Contributing

Contributions are welcome — see **[CONTRIBUTING.md](CONTRIBUTING.md)**.

AI-assisted workflows are allowed and may be used to support development, but
they must follow the directives in **[AGENTS.md](AGENTS.md)** scrupulously, and
human review is required before a pull request is opened.

## License

Orbit is licensed under the **Apache License 2.0**. See [LICENSE](LICENSE).
