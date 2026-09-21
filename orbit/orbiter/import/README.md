# Import machinery

This directory implements Orbit's module import system: turning an import
string into a canonical key, resolving the key to a concrete module, loading
and executing it exactly once per isolate, caching the result, and letting
concurrent and cyclic imports coexist without ever deadlocking the VM.

This document describes what it does today and calls out the parts that are reserved 
or incomplete in a dedicated section at the end.

## Contents

- [The language surface](#the-language-surface)
- [Import keys](#import-keys)
- [Search roots](#search-roots)
- [The pipeline](#the-pipeline)
- [Locators](#locators)
- [Loaders](#loaders)
- [`ImportSpec` and `__spec__`](#importspec-and-__spec__)
- [The registry: caching, concurrency, and cycles](#the-registry-caching-concurrency-and-cycles)
- [Errors](#errors)
- [Known gaps and reserved parts](#known-gaps-and-reserved-parts)
- [File layout](#file-layout)

## The language surface

From the grammar (`orbit/liftoff/grammar.ebnf`):

```orbit
import "io"                          # bind the module as `io`
import "io/bufio/reader" as bufio    # bind under another name
import Pattern, Match from "regex"   # bind selected members
import Readable as R from "io/traits"
pub import Pattern from "regex/pattern"   # re-export from the current module
pub import "regex/flags"                  # re-export a whole module
```

The default binding name is the last segment of the key (`"io/bufio/reader"`
binds `reader`). `pub import` is how a package's entry file assembles its public
surface out of its submodules; user code then sees one module.

## Import keys

An import string is a **logical key**, not a filesystem path. The separator is
`/`; there is no dotted form.

| Form           | Resolved against                       | Use for                                    |
|----------------|----------------------------------------|--------------------------------------------|
| `"x/y"`        | the search roots, in order             | stdlib, packages, anything shared          |
| `"./y"`        | the directory holding the importing file | a package's own submodules               |
| `"::orbit::x"` | the builtin table, verbatim            | engine primitives (stdlib internals only)  |

`Canonicalize` (`importer.cpp`) turns the raw string into the **canonical key**,
which is always absolute and OS-independent. The canonical key is the registry
cache key, so a module reached by two different spellings is still loaded once.

Rules, in order:

1. Empty string: `ImportError(INVALID_KEY)`.
2. `::` prefix: the builtin scheme. Only `[A-Za-z0-9_:]` is accepted after the
   prefix; the string is returned verbatim, with none of the rewriting below.
   `:` is illegal in a disk key, so builtins can never collide with a file.
3. Otherwise the string is walked segment by segment, splitting on `/` **or**
   `\`: empty segments (`//`, trailing `/`) and `.` segments are dropped, `..`
   is rejected with `INVALID_KEY`. Imports cannot walk upward; everything is
   rooted.
4. A leading `./` (or `.\`) is relative: the importing module's key is taken up
   to its last `/` and prefixed to the rest. Without an origin (the `-c`
   command line, or the entry script, see [gaps](#known-gaps-and-reserved-parts))
   this is `NO_ORIGIN`; from a non-`SOURCE` origin (a builtin) it is
   `INVALID_ORIGIN`.
5. A key that is empty after normalization (`"."`, `"/"`) is `INVALID_KEY`.

The directory holding the importing file is `dirname(name)` for a plain file
module and `name` itself for a directory-as-package (the entry file
`pkg/pkg.orb` has key `pkg` and lives in `pkg/`); `ImportSpec::is_package`
tells the two apart.

Examples: `"io"` stays `io`; `"a//b/./c"` becomes `a/b/c`; from the module
`pkg/sub`, `"./leaf"` becomes `pkg/leaf`; from the package entry `pkg`,
`"./sub"` becomes `pkg/sub`; from `pkg/sub`, `"../x"` is an error.

## Search roots

Roots are per isolate (`Importer::roots_`, a list of strings guaranteed to end
with the host separator) and are probed in insertion order. `SetupImportPath`
(`orbit.cpp`) installs, in this order:

1. the directory containing the `Orbit` executable;
2. `<that directory>/packages`;
3. every entry of `ORBIT_PATH` (`:`-separated, `;` on Windows), left to right.

So the bundled stdlib is found once it sits next to the executable; until the
build copies it there, `ORBIT_PATH=$PWD/stdlib` does the job.

For each root, the filesystem locator probes two spellings of a key:

| Probe                              | Meaning                                  | `is_package` |
|------------------------------------|------------------------------------------|--------------|
| `<root>/<key>.orb`                 | a file module                            | false        |
| `<root>/<key>/<basename(key)>.orb` | a directory-as-package (`io/io.orb`)     | true         |

The first hit wins; the file form beats the package form within the same root,
and an earlier root beats a later one. Existence is decided with `stat` on a
regular file, nothing else.

## The pipeline

```
raw string
   │  Canonicalize                (no lock)
   ▼
canonical key
   │  Acquire                     (registry lock, held for a few instructions)
   ├─ LOADED   ─────────────────────────────▶ return module
   ├─ PARTIAL  (cycle) ───────────────────────▶ return the partial module
   ├─ BLOCKED  (someone else is loading it) ──▶ fiber suspends, LDMOD re-runs on wake
   └─ FRESH    (we own the load)
         │  Resolve: builtin → fs-source       (no lock)
         ▼
      Descriptor
         │
         ├─ BUILTIN  adopt the ready-made module, PrepareCommit ──▶ return module
         │
         └─ SOURCE   compile the file, Prepare (module + __spec__ visible),
                     spawn an executor fiber for the top-level,
                     block the importer on the entry ──▶ BLOCKED
                              │
                     executor finishes: PublishResult → Commit / Fail
                              │
                     waiters wake, LDMOD re-runs → LOADED (or the panic)
```

Two properties fall out of this shape and are worth keeping in mind:

- **No lock is ever held while user code runs.** The registry lock
  (`cache_lock_`, an `AsyncRWLock`) guards only the hash maps. Resolution,
  compilation, and the module top-level all run unlocked.
- **`LDMOD` is re-entrant by design.** A `BLOCKED` outcome parks the fiber in
  `SUSPENDED_RETRY` and, when it is woken, the same `Import` call runs again
  from the top: the second time the entry is `LOADED` (or was removed and the
  panic is already on the fiber). Nothing is remembered across the two runs
  except the registry state.

## Locators

A locator answers "where is this key?" with a tri-state:

| Result     | Meaning                                        | Effect on the chain          |
|------------|------------------------------------------------|------------------------------|
| `NOT_MINE` | not this locator's business                    | try the next one             |
| `FOUND`    | resolved; `Descriptor` filled                  | stop, hand off to the loader |
| `ERROR`    | mine, but could not be delivered; panic is set | stop, propagate              |

`ERROR` exists so that a broken-but-present module is never reported as "not
found". Only when every locator says `NOT_MINE` does `Resolve` raise
`ImportError(MODULE_NOT_FOUND)`, listing the locators it tried (this is the
`tried: Builtin, FSSource` in the message).

The chain is fixed, in `Importer::Resolve`:

| Order | Locator    | Handles                           | Produces                                  |
|-------|------------|-----------------------------------|-------------------------------------------|
| 1     | `Builtin`  | `::orbit::*` keys only            | `BUILTIN` descriptor with the module built |
| 2     | `FSSource` | everything else, over the roots   | `SOURCE` descriptor with the on-disk path |

**`Builtin`** matches the key against the static table `kBuiltins`
(`locator.cpp`), one `ModuleInit` per engine module (`::orbit::builtin`,
`::orbit::chrono`, `::orbit::ffi`, `::orbit::gc`, `::orbit::io`,
`::orbit::runtime`). On a hit it constructs the module right there
(`ModuleTypeNew` + `ModuleNew`, then the optional `init` hook for slots that
need per-isolate values) and returns it in `Descriptor::module`. Nothing is
executed and no nested import can happen. The descriptor's `origin` is the key
itself.

**`FSSource`** declines `::` keys, then probes the roots as described above and
fills `origin` with the absolute path and `is_package` with the form that hit.

Builtins are primitives, not the idiomatic surface: user code imports `"io"`,
and it is `stdlib/io/io.orb` that does `import "::orbit::io"`. There is no
short alias for the builtin namespace.

## Loaders

`Descriptor::kind` selects how the module comes to life.

### `BUILTIN`

The module already exists. `Import` builds its `ImportSpec`, stores it in the
module's `__spec__` slot, and `PrepareCommit` attaches module and spec to the
entry and flips it to `LOADED` in one locked step. There is no observable
"prepared but not committed" window because there is no top-level to run.

### `SOURCE`

`LoadScriptSource` runs on the **importing** fiber: it opens the file, compiles
it with the liftoff pipeline (`liftoff::Compiler`, default optimization level),
creates the module type and instance, builds the `ImportSpec`, and `Prepare`s
the entry: from this moment the (empty) module and its `__spec__` are visible
to anyone who finds the entry in `LOADING`.

The top-level is **not** run by the importer. `Import` spawns a dedicated
executor fiber (`Orbiter::EvalDetached`) sharing the importer's `Context`,
records it as the entry's `owner`, enqueues the importing fiber on the entry's
waiters (`BlockOnExecutor`), pushes the executor to the scheduler, and returns
`BLOCKED`. When the executor's top-level ends, `Orbiter::PublishResult` sees
`fiber->module_entry` and calls `Commit` (clean return) or `Fail` (unhandled
panic).

Running the top-level on its own fiber is what makes the rest simple: the
importer is just another waiter, identical to a second fiber that happened to
import the same key, and there is exactly one "owner" per in-flight module for
cycle detection to reason about.

### `NATIVE` and `VIRTUAL`

Reserved. See [Known gaps](#known-gaps-and-reserved-parts).

## `ImportSpec` and `__spec__`

Every loaded module has a `__spec__` property (declared by `ModuleTypeNew`,
filled by the loader) holding an `ImportSpec`, an immutable Orbit object with
five public constant properties:

| Property     | Value                                                                 |
|--------------|-----------------------------------------------------------------------|
| `name`       | the canonical key: `"io"`, `"io/bufio/reader"`, `"::orbit::gc"`       |
| `origin`     | absolute on-disk path for `SOURCE`; the key itself for `BUILTIN`      |
| `loader`     | the `LoaderKind` as an integer: `0` builtin, `1` source, `2` native, `3` virtual |
| `is_package` | whether the directory-as-package form matched (see gaps: currently unreliable) |
| `locator`    | reserved for `VIRTUAL` modules; `nil` today                           |

```orbit
import "gc"
io.print(gc.__spec__.name, gc.__spec__.origin)   # gc /.../stdlib/gc.orb
```

The base for a module's relative imports is derived from `name` on demand;
nothing else is stored. The mutable load state lives in the engine-internal
`ModuleEntry`, never in the spec.

## The registry: caching, concurrency, and cycles

`Importer` holds, per isolate:

- `modules_`: canonical key → `ModuleEntry`;
- `wait_for_`: fiber → the entry it is currently blocked on (the wait-for
  graph);
- `cache_lock_`: one `AsyncRWLock` protecting both maps, taken in unique mode
  for every mutation and held only across map operations.

### `ModuleEntry`

```
LOADING ──▶ LOADED          (Commit: top-level returned)
   └──────▶ FAILED ──▶ gone  (Fail: top-level raised, or load never started)
```

| Field     | Set when                                              | Meaning                                                   |
|-----------|-------------------------------------------------------|-----------------------------------------------------------|
| `name`    | `Insert`                                              | canonical key                                             |
| `module`  | `Prepare` / `PrepareCommit`, before the top-level      | the module, partial while `LOADING`                       |
| `spec`    | same                                                  | the `ImportSpec`                                          |
| `owner`   | `BlockOnExecutor` (SOURCE only)                       | the executor fiber that will `Commit`/`Fail`; `nullptr` for builtins |
| `waiters` | `EnqueueAndWait`                                      | fibers to wake on `Commit`/`Fail`                         |

The entry is inserted **before** anything is resolved or compiled, so a second
importer of the same key always finds it and never starts a duplicate load.

### `Acquire`

The heart of `Import`, run under the unique lock:

| Registry state                                   | Outcome   | The caller then                                   |
|--------------------------------------------------|-----------|---------------------------------------------------|
| miss                                             | `FRESH`   | inserts a `LOADING` entry and drives the load     |
| `LOADED`                                         | `LOADED`  | returns `entry->module`                           |
| `LOADING`, `owner` is the calling fiber          | `PARTIAL` | returns the partial module (a module importing itself) |
| `LOADING`, blocking would close a wait-for cycle | `PARTIAL` | returns the partial module (see below)            |
| `LOADING`, otherwise                             | `BLOCKED` | is enqueued on `waiters`, suspends, re-runs `Import` on wake |
| allocation failure                               | `ERROR`   | propagates the panic                              |

### Cycles do not error

Consider `a.orb` importing `b`, and `b.orb` importing `a`. The executor of `a`
(call it `EA`) blocks on `b`'s entry, whose owner is `EB`. When `EB` reaches
`import "a"` it finds `a` in `LOADING` with owner `EA`; `HasCycle` walks
`owner → blocked-on entry → owner ...` from `EA`, finds `EB` itself, and
`Acquire` returns `PARTIAL`: `EB` gets `a`'s module as it is at that moment
and carries on. `EA` is woken when `b` commits, and `a` then completes.

The behavior is the usual one for circular imports: names defined in `a` 
before `import "b"` are visible to `b`, while names defined afterward 
read as `nil` at that point. The same mechanism handles longer cycles (`a → b → c → a`) 
and the truly concurrent case where two unrelated fibers start both halves of a 
cycle at the same time: whichever fiber would close the ring receives the partial module, 
so no fiber ever waits on itself, even transitively.

### Failure and retry

If the executor's top-level raises, `Fail` (still under the lock) marks the
entry `FAILED`, pushes the executor's panic onto every waiter with
`RaisePanic`, wakes them, removes the entry from, and frees it. The
importer therefore sees the module's own error, not a generic one, and a later
`import` of the same key starts from scratch and **re-executes** the file.
Nothing half-initialized lingers in the cache.

`Fail` is also the cleanup path when the load never got to an executor
(`Resolve` declined, `fopen` or compile failed, allocation failed): then there
are no waiters and it only drops the entry.

## Errors

All import failures are `ImportError` (`errors.h`) with one of:

| Reason                   | Message                                                     |
|--------------------------|-------------------------------------------------------------|
| `MODULE_NOT_FOUND`       | `module '%s' not found; tried: %s`                          |
| `INVALID_KEY`            | `invalid import key '%s': %s` (empty, `..`, bad builtin char) |
| `NO_ORIGIN`              | `relative import '%s' has no origin`                        |
| `INVALID_ORIGIN`         | `relative import '%s' is only valid from a source module`   |
| `LOADER_NOT_IMPLEMENTED` | `%s loader is not yet implemented (key '%s')`               |

A compile error in the imported file surfaces as the compiler's own error; a
panic in its top-level surfaces as that panic, on every fiber that was waiting
for the module. Importing inside a `sync` block is a `RuntimeError`
(`SUSPEND_IN_SYNC_CALL`).

## Known gaps and reserved parts

Things the code does not do yet, or does wrong, as of this writing. Each is
small; they are listed so nobody reads the sections above as a promise.

1. **`FSSource` also probes the shared-library extension** (`kExtension`
   ends with `.dylib` / `.so` / `.dll`) but tags every hit as `SOURCE`, so a
   `foo.so` next to the roots would be handed to the compiler. The `NATIVE`
   loader itself is a stub that raises `LOADER_NOT_IMPLEMENTED`. Until native
   modules land, the second extension should not be probed, or should
   produce a `NATIVE` descriptor.
2. **Unreadable files look like "not found".** `FSSource` treats any `stat`
   outcome other than "regular file" as `NOT_MINE`; a permission error should
   be `ERROR`. Likewise a failing `fopen` in `LoadScriptSource` drops the entry
   without setting an errno-based panic (there is a `TODO`).
3. **User locators and the `VIRTUAL` loader are reserved.** `LoaderKind::VIRTUAL`,
   `Descriptor::source`, `Descriptor::locator` and `ImportSpec::locator` exist
   so that a runtime-registered locator can one day return either an
   in-memory source or a ready-made module, inserted between `Builtin` and
   `FSSource` (`Resolve` has the `TODO` slot). Nothing produces them today and
   there is no `importlib` module.
