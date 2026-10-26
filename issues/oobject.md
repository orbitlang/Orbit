# oobject — bug report

**Component:** `orbit/orbiter/datatype/oobject.cpp` (object/type core: `MakeType`, property tables, type lifecycle) · **ID prefix:** `OBJ`
**PoCs:** [`poc/oobject/`](poc/oobject/) (OBJ-002, OBJ-003, OBJ-004; OBJ-001 has none, see below) · **Last reviewed:** 2026-10-26
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## OBJ-004 — A trait implemented by a superclass is not seen by `IsTypeExtends`
**Severity:** High (a normal call form is refused: the methods of a trait cannot be called on an instance of a subclass) · **Status:** FIXED (2026-10-26) · **Location:** `orbit/orbiter/datatype/oobject.cpp:13` (`IsTypeExtends`)

**Fix verified (PoC `poc/oobject/obj-004.orb` — prints `ALL TESTS PASSED`,
16 of 16; `ortest` 31/31 and the PoC gate 56/59, the failures being open
findings; `phi-regalloc.orb` green).** `IsTypeExtends` looks in the MRO of
every type along the superclass chain, as `TIFindProperty` and `GetSuper`
already do:

```cpp
for (auto *cursor = type; cursor != nullptr; cursor = O_GET_TYPE(cursor)) {
    if (cursor == target)
        return true;

    if (cursor->mro == nullptr)
        continue;

    const auto *mro = (Tuple *) cursor->mro;
    for (MSize i = 0; i < mro->length; i++) {
        if (mro->objects[i] == (OObject *) target)
            return true;
    }
}

return false;
```

The C3 code is left as it is, and that is the model, not an oversight: a
type's MRO holds the linearization of its own traits only (`BuildBasesList`
asserts every base is a trait), the superclasses form the separate chain
`O_GET_TYPE` walks, and a class with no traits of its own has no MRO.
`IsTypeExtends` was the only reader that did not follow it. Copying the
superclass's traits into every MRO instead would change the order
`TIFindProperty` searches in, so a trait's method would win over the
superclass's own override of it (`class A impl T { func f() }`,
`class B : A`: `new B().f()` would run `T.f`), and `GetSuper` would need
rework.

The PoC gained the cases the fix must cover without side effects: a trait
extended by the superclass's trait is seen from a subclass (`is` and a call),
a class's override still wins over its trait, `super` from a subclass still
reaches the override, and a superclass is still not its subclass. Run against
the old `IsTypeExtends`, 8 checks fail.

<details><summary>Original report</summary>

A subclass of a class that implements a trait is an instance of that trait,
but the runtime does not see it:

```orbit
trait T { pub func twice() { return self.v * 2 } }
class A impl T { pub var v = 21 }
class B : A { }

new A().twice()   // 42
new B().twice()   // TypeError: method call requires 'T' instance as receiver
new B() is T      // false
```

`C3::BuildMRO` returns at once when a class lists no traits of its own
(`if (length == 0) return true;`), so `B` has no MRO; a class that lists its
own traits (`class C : A impl U`) gets an MRO built from those alone. Neither
includes the traits of the superclass. `IsTypeExtends` reads the MRO of the
type it starts from only, then walks the superclass chain comparing the types
themselves, never their MROs, so `T` (in `A`'s MRO) is not found from `B`.
Property lookup walks the MRO of every type in the chain, which is why
`new B().twice()` finds the method and then refuses the receiver.

Every user of `IsTypeExtends` is affected: the receiver check of a method
(`CheckReceiver`, `pcheck.cpp`), `is` (`vm.cpp:953`), the protected-access
checks (`vm.cpp:467`, `vm.cpp:625`) and `MKMETH`.

**Found** while checking that VM-011's fix works for a trait method called
through a subclass (`B.twice(b)`); it fails the same way before that fix.

**PoC:** [`poc/oobject/obj-004.orb`](poc/oobject/obj-004.orb) *(confirmed
live: the three controls pass; the inherited trait method is refused through
an instance, a subclass with traits of its own, and a variable, and `is`
answers false twice)*.

**Fix (suggested, verified by probe, then reverted):** have `IsTypeExtends`
look in the MRO of every type along the superclass chain, not only the first:

```cpp
for (const auto *cursor = type; cursor != nullptr; cursor = O_GET_TYPE(cursor)) {
    if (cursor == target)
        return true;

    if (cursor->mro != nullptr) {
        const auto *mro = (Tuple *) cursor->mro;

        for (auto i = 0; i < mro->length; i++) {
            if (mro->objects[i] == (OObject *) target)
                return true;
        }
    }
}

return false;
```

With it the PoC passes 8/8 and the gates stay as they are (`ortest` 30/30,
the PoC gate unchanged apart from this PoC). The other way, making
`BuildMRO` merge the superclass's MRO into every class's, would also fix it,
but it changes the method resolution order that property lookup follows,
which needs its own review.

</details>

---

## OBJ-003 — `true` and `false` are named 'nil' wherever a type is printed
**Severity:** Low (wrong diagnostics and display) · **Status:** FIXED (2026-10-15) · **Location:** `orbit/orbiter/datatype/oobject.cpp:315` (`GetTypeName`)

**Fix verified (regression PoC `poc/oobject/obj-003.orb` — prints `ALL TESTS
PASSED`, 7 of 7; `ortest` 22/22; PoC gate unchanged apart from the open
findings; `phi-regalloc.orb` green).** `GetTypeName` now names both boolean
oddballs `Bool`, by type as every other type is named, in a single branch;
everything else that is neither a heap object nor an SMI keeps the `"nil"`
fallback, which is now reached by `nil` on purpose rather than by accident:

```cpp
else if (O_IS_FALSE(object) || O_IS_TRUE(object)) {
    memory::MemoryCopy(raw_name, "Bool", 4);

    length = 4;
}
```

Observed live:

```
io.print(trap t(), trap f(), trap n())   # ok<Bool> ok<Bool> ok<nil>
true + 1      # operator '+' not supported between 'Bool' and 'Number'
false()       # invalid call to a non-callable object('Bool')
nil + 1       # operator '+' not supported between 'nil' and 'Number'
```

No suite or stdlib module matched on the old `'nil'` text, and the PoC now pins
both names (`Bool` for a boolean, `nil` for nil). No `BOOLEAN` primitive was
added, deliberately: a name is all the messages need, and a type object would
only matter for something like `x is Bool`, which the language does not offer.
An intermediate version named the booleans by value (`'true'`, `'false'`); it
was replaced by `Bool` the same day for consistency with the other types.

<details><summary>Original report</summary>

`GetTypeName` files every value that is neither a heap object nor an SMI under
the `BOOLEAN` primitive:

```cpp
if (!O_IS_OBJECT(object)) {
    if (O_IS_SMI(object))
        type = isolate->primitive[(int) InstanceType::NUMBER];
    else
        type = isolate->primitive[(int) InstanceType::BOOLEAN];
}
...
memory::MemoryCopy(out_str, type != nullptr ? type->name : "nil", min_length);
```

No type is registered for `BOOLEAN` (`isolate.cpp` initializes no such
primitive), so `type` is null and the name falls back to `"nil"`. `nil` takes
the same road, since it is not a heap object either, and comes out right only
by that accident. Every message built from `GetTypeName` inherits it:

```
io.print(trap t())        # t returns true  -> ok<nil>
true + 1                  # operator '+' not supported between 'nil' and 'Number'
false.nope                # 'nil' object has no property 'nope'
```

The value itself is fine: `unwrap()` returns `true`. Only its name is wrong.

**Found** while checking VM-009: a recursion returning `true` through a `trap`
printed `ok<nil>`.

**PoC:** [`poc/oobject/obj-003.orb`](poc/oobject/obj-003.orb) *(confirmed live:
4 of 6 checks fail; the two about `nil` pass)*. It asks only that a boolean is
not called 'nil' and that `nil` still is, so it does not fix the new name in
advance.

**Fix (suggested, not tried):** give booleans a name of their own, either a
registered `BOOLEAN` primitive or a dedicated branch in `GetTypeName`, and tell
`nil` apart explicitly (`O_IS_NIL`) instead of through the null fallback.

</details>
---

## OBJ-002 — `GetTypeName` writes past the caller's buffer when a type name is long
**Severity:** High (stack buffer overflow on an everyday error path) · **Status:** FIXED (2026-10-15) · **Location:** `orbit/orbiter/datatype/oobject.cpp:336` (`GetTypeName`)

**Fix verified (regression PoC `poc/oobject/obj-002.orb` — prints `ALL TESTS
PASSED`, 5 of 5; `ortest` 22/22; PoC gate unchanged apart from the open
findings; `phi-regalloc.orb` green).** The copy now leaves room for the
terminator:

```cpp
const auto min_length = std::min(length, out_size - 1);
```

Checked by hand at 23, 24 and 25 characters and at 44: no abort at any length,
in an error message or in a printed Result. Two things the change could have
broken were checked across every caller: none passes an `out_size` of 0, where
`out_size - 1` would wrap (all pass `sizeof` of an array), and none uses the
return value, which is still the full length of the name and not the number of
bytes copied.

What remains is by design of the fix, not a defect: a name longer than 23
characters is cut short in messages, `'ConnectionPoolManagerFo'` for
`ConnectionPoolManagerForTheWholeApplication`. A larger buffer at the
error-message call sites, or formatting straight from `type->name`, would show
it whole.

<details><summary>Original report</summary>

```cpp
if (out_str != nullptr) {
    const auto min_length = std::min(length, out_size);

    memory::MemoryCopy(out_str, type != nullptr ? type->name : "nil", min_length);

    out_str[min_length] = '\0';
}
```

When the name is as long as the buffer or longer, `min_length` is `out_size`
and the terminator lands at `out_str[out_size]`, one byte past the end. Almost
every caller passes a `char[24]`: `ErrorSetWithObjType` (`error.cpp:365`),
which builds most of the runtime's `TypeError`s, `ResultToString`, the three
hook dispatchers in `ctbuilder.cpp`, and others across `oops.cpp`, `vm.cpp`,
`pcheck.cpp`, `stringformatter.cpp`, `iterator.cpp`, `future.cpp` and
`native/prepare.cpp`.

So a class whose name has 24 characters or more turns the most ordinary error
about one of its instances into a stack buffer overflow. With a 23-character
name all of these work; with 24:

```
x := new A23456789012345678901234()
x + 1          # SIGABRT
x()            # SIGABRT
x.nope         # SIGABRT
io.print(trap new A23456789012345678901234())   # SIGABRT
```

Under lldb the abort is `__stack_chk_fail` in `ResultToString`: the stack
protector of a debug build catches the write. A build without the protector
corrupts the byte after the buffer and carries on. Names that long are not
exotic (`ConnectionPoolManagerForTheWholeApplication`, or simply
`HttpConnectionPoolManager` at 25).

**Found** while looking into OBJ-003, in the same function.

**PoC:** [`poc/oobject/obj-002.orb`](poc/oobject/obj-002.orb) *(confirmed live:
SIGABRT at the first check)*. It asks only that the program survives and that
the name shows up in full or cut short, so it accepts either kind of fix. It is
expected to fail until this is fixed, per GUIDE §11.

**Fix (verified by probe, not applied):** leave room for the terminator,
`std::min(length, out_size - 1)`. With that change the PoC prints `ALL TESTS
PASSED`; the change was then reverted. Names are then cut at 23 characters in
messages, so a larger buffer at the error-message call sites, or formatting
the name straight from `type->name` where the length is known, is worth
considering as well.

</details>
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
