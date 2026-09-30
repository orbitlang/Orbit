# function — bug report

**Component:** `orbit/orbiter/datatype/function.cpp` (Function objects, `FuncShared` lifetime) · **ID prefix:** `FUNC`
**PoCs:** none (leak, not observable through the available tooling) · **Last reviewed:** 2026-09-23
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## FUNC-001 — Strong backpointers in `FuncShared` make modules and class types immortal
**Severity:** Low (leak; unbounded under repeated failed imports or dynamic type creation) · **Status:** OPEN · **Location:** `orbit/orbiter/datatype/function.cpp:170` (native), `:173` (`owner_type`), `:210-211` (interpreted)

A function keeps a **strong** reference to the context it belongs to:

```cpp
f_shared->module = O_FAST_INCREF(module);                   // native   (:170)
f_shared->owner_type = O_INCREF(owner);                     // method   (:173)
fn->shared->context = O_FAST_INCREF(fiber->context.context);// interp.  (:210)
fn->shared->module = O_INCREF(fiber->context.module);       // interp.  (:211)
```

The functions of a module live in that module instance's slots, so each of them
closes a reference cycle:

```
module instance ──slot──▶ Function ──shared──▶ FuncShared ──module──▶ module instance
```

The cycle is held together by refcounts, and this collector treats any object
with `rc > 0` as a root (`GC::ScanRoots`), so **neither end can ever be
reclaimed**. Tracing cannot break it either: `FunctionTrace` only reports
`closure` and `currying`, so the collector never even sees the edge as an edge.
The same shape applies to `owner_type` (a class type is pinned by every method
it exports) and to `context`.

This is not specific to native modules: the interpreted overload takes the same
reference from the current frame, so every module whose top level defines a
function is affected.

In normal operation nothing is lost, since modules stay in the importer registry
and class types stay reachable for the isolate's life. It leaks where a module
or a type is meant to go away:

- a module whose top level raises: `Importer::Fail` drops the registry entry,
  but any function its top level already defined pins the instance (and, through
  the instance, everything else in its slots). Retrying the failing import in a
  loop leaks once per attempt;
- types and modules built at runtime (`eval`, the REPL, a future
  `importlib`-style locator) accumulate for the same reason;
- it compounds with [OBJ-001](oobject.md): a type that could never be freed
  anyway also never releases what its properties hold.

**PoC:** none — a leak is not observable through the CLI without a leak checker;
verified by inspection (the four `INCREF` sites above, `FunctionTrace`, and
`GC::ScanRoots` treating `rc > 0` as a root). An ASan/LSan run over a script
that imports a failing module in a loop would surface it.

**Fix:** turn the backpointers into traced edges instead of counted references,
i.e. drop the `INCREF` in both `FunctionNew` overloads, drop the matching
`O_FAST_DECREF` in `FunSharedDel`, and report them from `FunctionTrace`:

```cpp
callback((OObject *) self, (OObject *) self->shared->module, epoch);
```

That is the convention the rest of the object model already follows (a traced
container does not incref what it holds), and it lets the collector see the
cycle and collect it as garbage once the registry drops the module.

Two things to check before making that change:

1. **Something else must root the module while it is in use.** Today the
   importer registry increfs it (`ModuleEntry::module`) and `GC::ScanFibers`
   visits `fiber->context.module` as a root, so a running frame is covered; a
   `Function` value held by user code with no live frame would then rely on the
   trace edge alone, which is exactly what the change is meant to establish.
   Verify there is no window between the two.
2. **`FuncShared` is refcounted and shared** between `Function` objects (currying
   and closures bump `refs`), so the edge would be reported once per `Function`
   that shares it. Tracing is idempotent, so that is harmless, but the DECREF
   removal has to stay symmetric with the `refs` bookkeeping.

---
