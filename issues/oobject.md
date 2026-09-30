# oobject — bug report

**Component:** `orbit/orbiter/datatype/oobject.cpp` (object/type core: `MakeType`, property tables, type lifecycle) · **ID prefix:** `OBJ`
**PoCs:** none (not observable through the available tooling, see below) · **Last reviewed:** 2026-09-23
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## OBJ-001 — A collected TypeInfo frees nothing: no type destructor, `aux.dtor` never runs
**Severity:** Medium (unbounded leak; latent null deref) · **Status:** OPEN · **Location:** `orbit/orbiter/datatype/oobject.cpp:151` (`MakeType`), `orbit/orbiter/memory/gc.cpp:178` (`GC::Free`)

A `TypeInfo` is an ordinary GC-tracked object (`MakeType` ends with
`O_GC_TRACK_RETURN(isolate, ti, false)`), so it is reclaimed like anything else
once its refcount drops to zero. What it owns, however, is never released:
nothing in the engine acts as a type destructor.

`GC::Free` runs `O_GET_TYPE(obj)->dtor`, which for a `TypeInfo` is the `dtor` of
the meta-type `Type`. `TypeSetup` (`type.cpp`) installs none, so the object's
memory is handed back and everything hanging off it is simply dropped:

| Leaked on reclaim | Set by |
|---|---|
| `ti->name` (heap copy of the type name) | `MakeType` (`allocator.Alloc`) |
| `ti->properties.p_array` | `TIPropertiesInit` |
| one `INCREF` per property **name** and per property **value** | `TIPropertyAdd` |
| `ti->mro` (an owned `Tuple`) | `c3.cpp:169` |
| `ti->aux.data`, via the **never-invoked** `ti->aux.dtor` | see below |

The property references are the worst of these: every value a type holds keeps
`rc >= 1` forever, and an object with `rc > 0` is a root for this collector
(`GC::ScanRoots`), so the whole graph reachable from a dead type stays live.

`aux.dtor` is the clearest symptom because it exists *specifically* to be called
and never is. Four call sites install one, and `grep` finds no caller:

```cpp
string->aux.dtor = (TypeInfoAUXDtor) StrGSTDtor;   // orstring.cpp:1982  (global string table)
atom->aux.dtor   = AtomGATDtor;                    // atom.cpp:88        (global atom table)
clazz->aux.dtor  = ClassBlueprintDtor;             // ctbuilder.cpp:368  (instance blueprint)
module->aux.dtor = ModuleAUXDtor;                  // module.cpp:170     (exported FunctionDef array)
```

The first two belong to isolate primitives that live as long as the isolate, so
they are moot; the last two are per class and per module and leak for real. Any
program that builds types at runtime — every `class` declaration, every module
type, the REPL, `eval` — leaks a little on each one.

Second, latent defect in the same area: `GC::Free` dereferences
`O_GET_TYPE(obj)` before checking it, and the root meta-type built by `TypeInit`
has `O_GET_HEAD(ti).type_ == nullptr` by construction (`MakeType` with
`super == nullptr`). Reclaiming that one object would null-deref. It is
unreachable today because the isolate holds the primitives for its whole life,
but the guard costs nothing.

**PoC:** none — a leak is not observable through the CLI or a linked probe
without a leak checker; verified by inspection (`grep` for `aux.dtor` callers,
plus reading `GC::Free`). A future ASan/LSan run over a script that declares
many classes would surface it.

**Fix:** give `TypeInfo` a real destructor and route reclaim through it.

1. Add a `TypeDtor(TypeInfo *)` and install it on the meta-type in `TypeSetup`,
   so `GC::Free` picks it up for every type. It must, in order: call
   `aux.dtor(self)` when set; `O_FAST_DECREF` every property name and value and
   free `properties.p_array`; release `mro`; free `name`.
2. Null-guard the type in `GC::Free` (`auto *type = O_GET_TYPE(obj); auto *dtor
   = type != nullptr ? type->dtor : nullptr;`) so the root meta-type is safe.
3. Audit the existing `aux.dtor` implementations once they actually run:
   `ModuleAUXDtor` and `ClassBlueprintDtor` only free their block, which is
   correct; `AtomGATDtor` / `StrGSTDtor` tear down the isolate-wide tables and
   must stay unreachable for anything but isolate teardown.

Note that a type reclaim also has to interact correctly with the refcount cycle
between a module instance, its native functions and their `shared->module`
backpointer (`function.cpp`): that cycle keeps module instances alive by design,
so it is the *type* side that this finding is about.

---
