# vm — bug report

**Component:** `orbit/orbiter` (interpreter: trap/panic unwind, register file) · **ID prefix:** `VM`
**PoCs:** [`poc/vm/`](poc/vm/) · **Last reviewed:** 2026-10-22
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

---

## VM-013 — `CallNative` reads rest and kwargs above SP and pops only the counted arguments
**Severity:** Low (latent: no native is variadic or takes keyword arguments today) · **Status:** OPEN · **Location:** `orbit/orbiter/vm.cpp:486` (`CallNative`), callers `vm.cpp:123` (`Call`) and `vm.cpp:91` (`ExecDefer`)

`CallNative` finds the arguments of a native function from SP and the bound
count alone, and hands the two slots after them to the native as the rest
list and the kwargs dict:

```cpp
const auto offset = argc * sizeof(void *);
auto **args = (OObject **) (stack->stack + (regs->SP.reg - offset));

const auto result = func->shared->func(func, args, args[argc], args[argc + 1], argc);
...
regs->SP.reg -= offset;
```

`ArgumentBinder::Bind` lays out a call as the counted parameters followed by
the rest list and the kwargs dict, which it pushes without counting them in
`StackArgs()` (`FinalizeRestArgs`, `FinalizeKwargs`). The two layouts disagree
whenever either slot is pushed:

- with no rest list and no kwargs dict on the stack (every call today),
  `args[argc]` and `args[argc + 1]` are the two slots **above** SP: stale
  values from earlier calls, or past the end of the stack buffer when the
  stack is full;
- with them pushed (a native declared variadic or taking `*kwargs`), `args`
  starts one or two slots too high, so every argument is read shifted, and
  `SP -= offset` leaves the uncounted slots on the stack, which corrupts the
  caller's frame the way a leaked argument slot does.

Nothing triggers it now: all 215 native functions and methods are declared
with `varargs` and `kwargs` false, so `Bind` never pushes the two slots for a
native (a rest list is consumed into the declared parameters or refused, a
kwargs dict is refused with `NO_KWARGS`), and no native reads its `rest` or
`kwargs` arguments. The first native that declares either would hit it.

**Found** while checking how the rest list and the kwargs dict reach the
callee, for VM-011's `DropReceiver`.

**PoC:** none — not triggerable from Orbit while no native is variadic or takes
keyword arguments; verified by inspection (`CallNative`, `FinalizeRestArgs`,
`FinalizeKwargs`, and the `varargs`/`kwargs` flags of every `RUNTIME_FUNCTION`
and `RUNTIME_METHOD`).

**Fix (suggested, not tried):** take the region from the binder instead of
recomputing it: the arguments start at `ArgsBaseSP()`, the rest list and the
kwargs dict are passed only when the function is variadic or takes kwargs
(`nullptr` otherwise), and SP goes back to `ArgsBaseSP()`. `ExecDefer` calls
`CallNative` with a saved `argc` and also has `restorable_SP`, the same value.

---

## VM-012 — A method called without a selector does not check its receiver
**Severity:** High (reads the wrong slots of a foreign object; SIGSEGV with a non-object) · **Status:** FIXED (2026-10-22) · **Location:** `orbit/orbiter/orbcall.cpp:128` (`ArgumentBinder::CheckReceiver`), `orbit/orbiter/orbcall.cpp:204` (`ArgumentBinder::Bind`)

**Fix verified (PoC `poc/vm/method-receiver-plain-call.orb` — prints `ALL
TESTS PASSED`, 9 of 9; `ortest` 30/30 and the PoC gate 54/58, the failures
being open findings; `phi-regalloc.orb` green).** The receiver check runs in
every call mode, not only for selector calls. `NormalizeMethod` became
`CheckReceiver`, which finds `self` wherever the call carries it: bound by
currying, pushed by a selector, passed as the first argument, or the first
element of a rest list. A call that carries none of them (`add()`,
`add(x=1)`) binds only other arguments, and `self` is checked when a later
call brings it. `Bind` calls it before the partial-application branch:

```cpp
if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD) && !this->func->shared->IsMethod())
    this->stack_args -= 1;
else if (!this->CheckReceiver()) {
```

So a `self` is checked when a partial application binds it, and a currying
that holds one can be trusted afterwards. A promoted method (a function a
decorator turned into a method of a class) is still exempt: the method it
wraps checks its own receiver, which this fix makes true for every call.
Native methods are checked here too now when called without a selector;
`pcheck.cpp` checks them again, with the same message.

The error now names the class the method expects ("method call requires 'A'
instance as receiver"), as `pcheck.cpp` does; it used to name the type of the
rejected receiver, which read backwards.

The PoC gained the three other ways in (a rest list, a pipe, a partial
application). With the check restricted to selector calls again, four of them
fail and the number crashes. The VM-011 PoC gives the same output before and
after the change.

<details><summary>Original report</summary>

A method is an ordinary function whose first parameter is `self`, and it can
be taken out of its class and called without a selector:

```orbit
get := A.get
get(new A())     // 4
```

`Bind` checks the receiver against the method's class only for a call in
`CallMode::METHOD`, the mode of a selector call:

```cpp
if (ENUMBITMASK_ISTRUE(mode, CallMode::METHOD) && !this->NormalizeMethod()) {
```

A call without a selector is not in that mode, so its first positional
argument becomes `self` with no check. The method's code reads `self` with its
own class's slot layout (the inline `self.x` path), so:

- an instance of an unrelated class is read at the wrong offsets:
  `get(new Other())` returns `Other`'s first slot as if it were `A.v`, with no
  error;
- a number as receiver crashes the process: `get(5)` is a SIGSEGV.

The first argument is not the only way in. A rest list (`add([o, 3]...)`) and
a pipe (`o |> add(5)`) deliver the receiver unchecked too, and so does a
partial application: `p := add(new Other())` has too few arguments, so `Bind`
returns a curried function holding the unchecked `self`. From then on that
`self` counts as bound: a selector call checks the bound value only against
the class of the method it calls through, and a decorator that returns such a
function has it installed as it is. *(Added 2026-10-22.)*

Native methods are not affected: `pcheck.cpp` checks `argv[0]` against the
owner type of every native method, whatever the call mode.

**Found** while weighing how a function promoted to a method by a decorator
should check its receiver: whether it can skip the check depends on the
methods it calls checking theirs, and a plain call of a method does not.

**PoC:** [`poc/vm/method-receiver-plain-call.orb`](poc/vm/method-receiver-plain-call.orb)
*(confirmed live: the four controls pass — right receiver, subclass, call
through an instance, a curried method with `self` bound — the unrelated
instance is accepted, and the number crashes)*.

**Fix (suggested, not tried):** check the receiver of a method in `Bind`
whatever the call mode, when `self` is not already bound by currying: in
`CallMode::METHOD` it is the receiver slot `NormalizeMethod` already looks at,
otherwise it is the first positional argument, or the first element of the
rest list when there are no positional arguments. The check has to come
before the partial-application branch of `Bind`, so that a `self` is checked
when it is bound; a curried method whose currying is not empty then had its
`self` checked by the call that bound it.

</details>

---

## VM-011 — A method called through its type receives the type as its receiver
**Severity:** High (a normal call form is refused; a type object passes as `self` and is read with an instance layout) · **Status:** FIXED (2026-10-22) · **Location:** `orbit/orbiter/orbcall.cpp:228` (`ArgumentBinder::Bind`), `orbit/orbiter/orbcall.cpp:138` (`ArgumentBinder::CheckReceiver`)

**Fix verified (PoC `poc/vm/method-through-type.orb` — prints `ALL TESTS
PASSED`, 12 of 12; `ortest` 30/30 and the PoC gate 55/58, the failures being
open findings; `phi-regalloc.orb` green).** Both halves, in `orbcall.cpp`
only; `PUSHIF` and the compiler are unchanged.

1. In the `CallMode::METHOD` block of `Bind`, a type used as selector is not
   a receiver: when the pushed slot holds a type object and the callee is a
   method of anything but a metatype, `DropReceiver` removes it (the explicit
   arguments move down by one, SP and `stack_args` decrease) before `base_sp`
   is fixed, and the first explicit argument is checked as in any other call:

   ```cpp
   if (!this->func->shared->IsMethod())
       this->stack_args -= 1;
   else if (IsTypeObject(*this->ArgsBase()) && !IsMetatype(fiber->isolate, this->func->shared->owner_type))
       this->DropReceiver();
   ```

   The test is on the selector, not on the method's class: `Handler.handle(h)`
   (a method promoted from `Server`) and `Handler.handle2(h)` (a curried
   method of `Server`) drop `Handler` too, and `h` arrives as `me`. A check
   against the method's class would have left `Handler` on the stack as `me`.
2. `CheckReceiver` no longer checks a type receiver through `O_GET_TYPE` (its
   superclass): a type object is `self` only for a method of a metatype
   (`Type`, or `Class` for classes; none has methods today).

`PUSHIF` stays: it still saves the push, and with it a shift of every
argument, for the common selector call of a function that is not a method
(`io.print(...)`, a callback in a property).

The PoC passes in full: calls through a class, a subclass and a builtin type;
`D.get(new P())` runs (checked against `P`); `P.get()` is a partial
application; `D.who()` no longer returns the class; `get(D)` is refused.
`ortest/decorators/04_decorator_objects.orb` gained the two decorated methods
called through their class. Mutation-tested: without `DropReceiver` seven
checks fail again, without the type refusal in `CheckReceiver` `get(D)` is
accepted again. `poc/vm/trap-bind-error-argpos.orb` still passes.

Found on the way, filed apart: OBJ-004 (a trait implemented by a superclass
is not seen by `IsTypeExtends`, so its methods refuse instances of a
subclass).

<details><summary>Original report</summary>

`Type.method(obj, args...)` should run `method` with `obj` as its receiver,
as the same method taken into a variable does (`m := Type.method; m(obj)`,
which works). Instead it is refused, for classes and builtin types alike:

```
P.get(p)            -> TypeError: method call requires 'P' instance as receiver
List.append(l, 3)   -> TypeError: method call requires 'List' instance as receiver
```

A selector call emits `PUSHIF`, which pushes the selector's base as the
receiver whenever the callee is a method. Here the base is the type object, so
the type is pushed and `obj` becomes the first ordinary argument;
`CheckReceiver` then checks the type object against `owner_type` and refuses
it. The explicit receiver never takes the receiver's place.

The user confirmed (2026-10-16) that this is a defect: calling a method
through its type is meant to work, with the first argument as the receiver.

**Second half (added 2026-10-22): a type object passes as `self`.**
`CheckReceiver` treats a receiver that is not an instance as a type and checks
it through `O_GET_TYPE`, which for a `TypeInfo` is its superclass. So a
subclass is accepted as `self` for every method of its superclass:

```orbit
class P { pub var v = 5; pub func get() { return self.v }; pub func who() { return self } }
class D : P { }

D.who()        // ok<D>: self is the class D
D.get()        // reads self.v out of the type object with P's layout, no error
get := P.get
get(D)         // the same, without a selector
```

No method takes a type as `self` on purpose: `Class` and `Type` have no
methods. Instrumenting the check over every `ortest` suite and PoC showed type
receivers in two files only, both expecting the refusal (this PoC and
`poc/vm/trap-bind-error-argpos.orb`, which uses `K.get(42)` to raise a binding
error).

**Decided (2026-10-22):**

- `P.get()` with no receiver left is a partial application, as
  `get := P.get; get()` is, not an error;
- the receiver is checked against the method's class, not against the type
  used as selector: `D.get(new P())` runs.

**Found** while discussing decorated methods (IR-011), where calling a
decorated method with an explicit receiver is one of the shapes to support.

**PoC:** [`poc/vm/method-through-type.orb`](poc/vm/method-through-type.orb)
*(confirmed live: both controls pass, the five calls through a type are
refused; a receiver of the wrong type is refused, as it must stay. Since
2026-10-22 also: `D.get(new P())` is refused, `P.get()` is refused instead of
being a partial application, and `D.who()` and `get(D)` accept the class as
`self`)*.

**Fix (suggested, not tried):** two parts.

1. In `Bind`, not in `PUSHIF`. A selector call always counts a receiver slot
   (`arguments = n + 1`); `PUSHIF` pushes it only for a method, and `Bind`
   infers it was pushed from the same `IsMethod()`. If `PUSHIF` skipped the
   push for a type base, `Bind` could not tell and would read a caller's slot
   as `self`. So `PUSHIF` stays as it is, and in the `CallMode::METHOD` block
   of `Bind`: when the pushed receiver is a type object and the method belongs
   to it or to a supertype (`IsTypeExtends(base, owner)`), drop that slot —
   move the explicit arguments down by one, then decrease SP and
   `stack_args` — before `base_sp` is fixed. `CheckReceiver` then sees the
   first explicit argument, as in any other call. This covers classes,
   subclasses, traits (`T.m(obj)`) and natives (`List.append(l, 3)`).
2. In `CheckReceiver`, drop the `O_GET_TYPE` branch: a type object is not a
   valid receiver, except for a method of `Class` or `Type` (none exists
   today; the rule covers them if they come).

`poc/vm/trap-bind-error-argpos.orb` keeps working: `K.get(42)` then has `42`
as its receiver, which is refused.

</details>

---

## VM-010 — A curried call drops its named arguments and `*kwargs`
**Severity:** Medium (silent wrong result: the final call runs with the defaults) · **Status:** OPEN · **Location:** `orbit/orbiter/orbcall.cpp:229` (`ArgumentBinder::Bind`, the currying path)

When a call has fewer positional arguments than the function's arity, `Bind`
returns a curried function instead of calling it. The curried function is
built from the positional arguments alone:

```cpp
this->fiber->vm.regs.RR.reg = (PtrSize) FunctionNew(this->func, this->ArgsBase(), this->stack_args).get();
```

The named arguments (`r10`, `nargs`) and the keyword dict (`r12`, `kwargs`) of
that call are not saved anywhere, and nothing reports it. When the curried
function is completed, the function sees its defaults again:

| Call | Result | Expected |
|---|---|---|
| `g(1, c=9)(2)` on `func g(a, b, c=5)` | 125 | 129 |
| `g(c=9)(1, 2)` | 125 | 129 |
| `kw(1, *{"x": 1})(2)` on `func kw(a, b, *rest)` | `rest` is `{}` | `{"x": 1}` |

The same full calls, with every positional argument given, are right.

**Why it matters now.** Decorators lean on currying for the case of a
decorator configured once and applied many times: with the decorated function
as the first parameter (`func times(f, n=2)`), `by7 := times(n=7)` should be a
decorator waiting for the function, and `@[by7]` should apply `times(f, n=7)`.
Today `@[by7]` applies `times(f)` with the default `n=2`.

Related, to be decided along with the fix: only a parameter with a default can
be named (by design, see `ortest/calls/01_argument_passing.orb`), but naming a
**required** one is not refused. `g(1, b=2)` on `func g(a, b)` takes the same
currying path, drops `b=2`, and returns a curried function where an error
(or a bound `b`) would be expected. A name the function does not have at all
is refused (`'g' does not accept named arguments`).

**Found** while discussing pre-configured decorators with the user.

**PoC:** [`poc/vm/curry-named-args.orb`](poc/vm/curry-named-args.orb)
*(confirmed live: the three controls pass, the five curried checks fail)*.

**Fix (suggested, not tried):** keep the call's `nargs` and `kwargs` in the
curried function next to the positional arguments, and merge them into the
completing call (the completing call's own names win, or a name given twice is
refused: to be decided). The required-parameter case needs its own rule:
either refuse the name, or let a named required parameter count towards the
arity.

---

## VM-009 — An exception context dangles once the VM stack is reallocated
**Severity:** High (use after free; crash, or silent reads of freed memory) · **Status:** FIXED (2026-10-16) · **Location:** `orbit/orbiter/vm.cpp:1962` (`TARGET_OP(TBGIN)`), `orbit/orbiter/vmstack.cpp:36` (`VMStack::Grow`), `orbit/orbiter/excstack.h` (`ExPtrContainer`)

**Fix verified (regression PoC `poc/vm/exception-context-stack-move.orb` —
prints `ALL TESTS PASSED`, 8 of 8 — plus two new PoCs,
`poc/vm/exception-context-module-slot.orb` and
`poc/vm/panic-without-handler.orb`; `ortest` 22/22; PoC gate 45/46, the open
IR-010 being the only failure; `phi-regalloc.orb` green).** `CP` and the link
to the previous context are no longer pointers but offsets from the stack base,
like `SP` and `BP`, and become pointers only where a context is read or
written:

```cpp
// TBGIN
const auto ctx = new(location)ExceptionContext(regs->CP.reg, regs->BP.reg, catch_off);
regs->CP.reg = ctx->GetOffset(fiber->vm.stack.stack);   // offset + 1

inline ExceptionContext *ExContextGet(const void *base, const U32 offset) {
    if (offset == 0)
        return nullptr;
    return (ExceptionContext *) ((unsigned char *) base + (offset - 1));
}
```

Three details of the encoding are load-bearing, and each has a check that
fails without it (mutation-tested by swapping each one back in):

- **The offset is biased by one.** `CP == 0` still means "no context", and a
  context can really sit at offset 0: the module frame has `BP == 0` and its
  first reserved slot is slot 0. Without the bias the first module-level `try`
  reads as "no context" and `TEND` dereferences null: 6 of 11 `vm` PoCs and 8
  of 22 `ortest` suites crash, `exception-context-module-slot.orb` among them.
- **"No context" maps to `nullptr`, not to the stack base.** An intermediate
  version without the bias also lacked the zero check, so `CP == 0` became the
  base of the stack and every `!= nullptr` test passed: a panic with no handler
  read the module's own slots as a context and jumped to a garbage catch
  offset (`Assertion failed: (false)` in the dispatch loop). Only
  `panic-without-handler.orb` catches it; `ortest` stays 22/22.
- **`prev` is shifted, and `SetAction` overwrites.** The previous CP is stored
  as `offset_prev << 2` so the pending action keeps the two low bits, and
  `prev()` shifts it back. `SetAction` must clear the old action before writing
  the new one: an intermediate version OR-ed them, so a pending `return` (3)
  replaced by a `break` (1) still read as `return`. Caught by `ortest`
  `exceptions/01_abrupt_exit.orb` and by the new last check of the stack-move
  PoC, which raises the `break` after the stack grew.

The stack-move PoC gained three checks: two contexts in one frame, each above
a growth (the inner one is released first and CP must point back at the outer
one); a `return` pending in a `finally` across a growth; and a pending `return`
replaced by a `break` raised after a growth.

**Audit of other absolute stack addresses**, as suggested in the original
report. The frame prologue saves `SP` and `BP` as offsets and `IP` as a code
address, so it is unaffected. A generator that yields inside a `try` and is
resumed from 50 frames deeper still catches (probe, same result before and
after the fix). The other `stack->stack + ...` expressions build a pointer for
immediate use, with one case left unverified: `CallNative`, `CallFunction`
and `EvalAsync` hand a native function an `args` pointer into the stack, which
would dangle if the native called back into Orbit on the same fiber and the
callback grew the stack before the native read its arguments again. Not
reproduced, and not filed until it is.

<details><summary>Original report</summary>


`TBGIN` builds a `try` or `trap` context inside the stack buffer and keeps its
**absolute** address:

```cpp
const auto location = stack->stack + (regs->BP.reg + slot);
const auto ctx = new(location)ExceptionContext((ExceptionContext *) regs->CP.reg, ...);
regs->CP.reg = (PtrSize) ctx;
```

That address lives on in `CP` and in the `prev` link of every context opened
after it. Growing the stack is a `realloc`, which may move the buffer, and it
does: under lldb the first growth, from 16 KB to 21 KB, moved it from
`0x7548c0c010` to `0x7549060010`. `SP` and `BP` are offsets into the buffer and
follow it; `CP` and `prev` are pointers and keep pointing into the freed block.

- **A context in every frame crashes.** A recursion that opens a `trap` in each
  frame dies with `SIGSEGV` in `TEND` (`ExPtrContainer::action` on a null
  context) at a depth between 500 and 1,000, the first growth of the stack.
- **A single context above a deep recursion only seems to work.** `trap deep(0)`
  over 50,000 frames returns the right value, but at its `TEND` under lldb
  `CP` was 8 MB below the live buffer: it reads the freed block, which happens
  to be intact.

Not a regression from the VM-007 fix: the crash reproduces identically with the
old `Grow`, since the stack has always grown by `realloc`.

**Found** while extending the VM-004 PoC to its last uncovered refusal, a stack
exhausted inside `Bind`: reaching it in the frame of a `trap` needs a `trap` in
every frame of a deep recursion.

**PoC:** [`poc/vm/exception-context-stack-move.orb`](poc/vm/exception-context-stack-move.orb)
*(confirmed live: SIGSEGV at the third check; the same file at a depth of 300,
where the stack never grows, prints `ALL TESTS PASSED`)*. It is expected to fail
until this is fixed, per GUIDE §11.

**Fix (suggested, not tried):** keep `CP` and the `prev` links as offsets from
the stack base, as `SP` and `BP` already are, and turn them into pointers only
where a context is read or written. Rebasing every live context inside `Grow`
would also work, but it has to find all of them, saved frames included, and
anything else that stores an absolute stack address would still be wrong.
Worth auditing at the same time for other absolute pointers into the stack
held across a call (the frame prologue saves `Context | SP | BP | IP`).

</details>

---

## VM-008 — `PUSHIF` reads a type object as a Function
**Severity:** High (undefined read; confirmed live as a corrupted call) · **Status:** FIXED (2026-10-15) · **Location:** `orbit/orbiter/vm.cpp:1417` (`TARGET_OP(PUSHIF)`)

**Fix verified (new regression PoC `poc/vm/pushif-type-target.orb` — prints
`ALL TESTS PASSED`, 11 of 11; `ortest` 22/22; PoC gate unchanged apart from
the two open findings; `phi-regalloc.orb` green).** `PUSHIF` keeps what
`ResolveCallable` returns and asks it, not the original target:

```cpp
auto target = (Function *) ACCESS_REG_SRC(instr);
...
target = ResolveCallable(fiber->isolate, target);
if (target == nullptr)
    goto ERROR;

if (!target->shared->IsMethod()) {
    DISPATCH;
}
```

The other half of the original question, whether a constructor reached
through a selector binds its receiver the way `Bind` expects, holds: the five
builtin types with a constructor (`Bytes`, `Set`, `Error`, `Chan`, `Context`)
all build it with `RUNTIME_FUNCTION`, never as a method, and an Orbit class has
no constructor of its own to be called through (`K(1)` is refused). So `PUSHIF`
pushes no receiver, `NormalizeMethod` drops the slot reserved for one, and the
two agree.

The PoC calls each of those five types through a selector and compares the
result with a direct call, with keyword arguments, several arguments and none;
it also checks a refused constructor call in argument position and a selector
to something that is not callable. **The defect is now confirmed live, not
only by inspection**: run against the old `PUSHIF`, the bytes near the name
`Error` read as a method flag, the receiver was pushed, and the refused call
`seen_mid("A", trap err.t(), "C")` handed the callee the `Holder` object in
place of `"A"`. The other checks happened to pass there, which is why the first
probe (`Bytes`) found nothing.

<details><summary>Original report</summary>

A method-style call (`obj.attr(...)`) first runs `PUSHIF METHOD`, which pushes
the receiver only when the target is a method:

```cpp
if (ResolveCallable(fiber->isolate, (Function *) target) == nullptr)
    goto ERROR;

if (!((Function *) target)->shared->IsMethod()) {
    DISPATCH;
}
```

`ResolveCallable` accepts two kinds of target: a Function, and a type object
with a constructor, which it resolves to that constructor. The test that
follows ignores the resolution and casts the original target. For a type object
`((Function *) target)->shared` is `TypeInfo::name`, a `const char *`, so
`IsMethod()` reads the `kind` flags of a `FuncShared` that is really the bytes
around the type's name, and pushes the receiver or not according to them.

**PoC:** none. The one shape checked, a builtin type reached through a selector
(`h.t(len=4)` with `h.t = Bytes`), happens to read a clear bit and behaves; the
outcome depends on the bytes near each name, so a reproducer would test the
memory layout, not the defect. Found *by inspection* while verifying VM-004.

**Fix (suggested, not tried):** test the resolved callable,
`auto *callee = ResolveCallable(...); ... if (!callee->shared->IsMethod())`.
Then check that a constructor reached through a selector binds its receiver the
way `Bind`'s `NormalizeMethod` expects.

</details>
---

## VM-007 — Past about 510,000 frames, every call reallocates the whole stack
**Severity:** High (a bottomless recursion hangs for minutes instead of raising a stack overflow) · **Status:** FIXED (2026-10-15) · **Location:** `orbit/orbiter/vmstack.cpp:36` (`VMStack::Grow`)

**Fix verified (regression PoC `poc/vm/stack-growth-near-limit.orb` — prints
`ALL TESTS PASSED` in 1.2 s; `ortest` 22/22; PoC gate unchanged apart from the
open IR-010; `phi-regalloc.orb` green).** When the 30% step would cross the
limit, the stack now takes everything that is left in one step, and refuses
only when even that is not enough for the request:

```cpp
if (this->capacity + increment >= this->limit) {
    increment = this->limit - this->capacity - 1;
    if (increment < size)
        return false;
}
```

`limit - capacity - 1` cannot wrap: the capacity reaches `limit - 1` at most,
where the difference is 0 and the request is refused. Measured across the old
cliff, every depth now answers in about 0.2 s, and the deepest recursion that
fits is the same as before (520,000 frames of a one-argument function
complete, 525,000 raise `OOMError: stack overflow`), so the whole 32 MB is
still usable. Refusing as soon as the 30% step would cross the limit, which was
also considered, would have stopped the stack at the last 30% step instead:
close to the limit with today's numbers, but as low as `limit / 1.3` in
general.

[VM-009](#vm-009--an-exception-context-dangles-once-the-vm-stack-is-reallocated)
was found while checking this fix and is unrelated to it: it reproduces with and
without the change.

<details><summary>Original report</summary>

```cpp
auto increment = std::max(size, (this->capacity * kStackGrowthFactor) / kStackGrowthScalingFactor);

if (this->capacity + increment >= this->limit) {
    increment = size;
    ...
}
```

The stack grows by 30% a step until that step would cross `kMaxStackSize`
(32 MB). From then on each growth is exactly the bytes asked for, so every new
frame in the last stretch before the limit reallocates, and may copy, a stack
of about 31 MB. A recursion through that stretch is quadratic:

| depth (one-argument function) | time |
|---|---|
| 400,000 | 0.19 s |
| 510,000 | 0.19 s |
| 520,000 | > 15 s (CPU-bound) |
| bottomless | minutes before the stack overflow error |

The limit itself is right, and so is the error at the end of it; only the way
there is wrong. The hang in the original VM-004 report was this stretch,
reached by leaked argument slots instead of by frames.

**PoC:** [`poc/vm/stack-growth-near-limit.orb`](poc/vm/stack-growth-near-limit.orb)
*(confirmed live: times out after 30 s)*. It is expected to fail until this is
fixed, per GUIDE §11.

**Fix (verified by probe, not applied):** when the 30% step would cross the
limit, grow to just below the limit in one step:
`increment = std::max(size, this->limit - this->capacity - 1)`. With that
change the PoC prints `ALL TESTS PASSED` in 0.6 s, a bottomless recursion
raises `OOMError: stack overflow` in 0.2 s, and the deepest recursion that fits
is unchanged (520,000 frames complete, 530,000 overflow); the change was then
reverted.

</details>
---

## VM-006 — `!x` disagrees with `if x`: integers always negate to true, falsy objects to false
**Severity:** High (silent wrong boolean, and wrong branch in `if !x`) · **Status:** FIXED (2026-10-14) · **Location:** `orbit/orbiter/vm.cpp:980` (`TARGET_OP(NOT)`)

**Fix verified (regression PoC `poc/vm/not-truthiness.orb` — prints `ALL TESTS
PASSED`, 13 of 13; `ortest` 22/22; `phi-regalloc.orb` green).** `NOT` now asks
the same question as the conditional jumps and the ternary:

```cpp
ACCESS_REG_DST(instr) = BOOL_TO_OBOOL(!IsTrue(value));
```

So `!3` is `false` and `if !n` with `n = 3` skips its branch, while `!""`,
`![]`, `!{}`, `!b""` and `!` of an error Result are `true`.

The rest of the path was checked as the original report asked:

- the compiler emits `NOT` in one other place, `!=` and `!==`, lowered to
  `NOT(EQ)` (`irbuilder.cpp:762`). `EQ` yields a boolean, so the result is
  unchanged; checked live, and `ortest/typehooks/tostr_hooks.orb` (including
  `neq hook` and `eq non-bool traps`) stays green;
- `optimizer.cpp` folds nothing involving `!`;
- every `!` in the stdlib (`repl.orb`, `io/io.orb`, `io/bufio`, `io/file.orb`,
  `regex/pattern.orb`) applies to a boolean (`is`, comparisons, `closed`,
  `failed`, `matched()`), on which the old and the new opcode agree, so no
  stdlib code relied on the defect.

`ortest/net/02` and `04` are back to `check_true(name, !!r)` for a Result;
while this was open they had to spell it `r ? true : false`.

<details><summary>Original report</summary>

The `NOT` opcode decides truth on its own instead of asking `IsTrue`, which is
what the conditional jumps (`JF`, `JT`) and the ternary use:

```cpp
ACCESS_REG_DST(instr) = ((MSize) value == kOddBallTRUE || O_IS_OBJECT(value))
                            ? kOddBallFALSE
                            : kOddBallTRUE;
```

Only `true` and heap objects count as true here. `IsTrue` (`oops.cpp:467`)
treats an SMI as true when it is not zero and lets a heap object answer through
its type's `to_bool`. The two disagree in both directions:

```orb
n := 3
if !n { ... }          // taken: !3 is true
!1                     // true
!""  ![]  !{}  !b""    // false, though each is false to a conditional
!(trap boom())         // false, though an error Result is false to a conditional
```

Every other case checked agrees: `nil`, `false`, `true`, `0`, non-empty
containers, an ok Result.

**Found** while writing the IPV6ONLY checks of `ortest/net/`: a check written
as `check_true(name, !!r)` on an error Result passed for the wrong reason. The
suites use `r ? true : false` instead.

**PoC:** [`poc/vm/not-truthiness.orb`](poc/vm/not-truthiness.orb) *(confirmed
live: 6 of 13 checks fail)*. Each check compares `!x` with
`x ? false : true`. It is expected to fail until this is fixed, per GUIDE §11.

**Fix (suggested, not tried):** `ACCESS_REG_DST(instr) = BOOL_TO_OBOOL(!IsTrue(value))`.
Then check whether the compiler folds or rewrites `!` anywhere else (constant
folding in `optimizer.cpp`, `if !x` lowered to a jump), and audit the stdlib
for code written around the current behaviour.

</details>
---

## VM-005 — Integer literals of 2^32 and more load as their value shifted right by 32
**Severity:** High (silent wrong value for a whole range of integer literals) · **Status:** FIXED (2026-10-14) · **Location:** `orbit/orbiter/vm.cpp:1267` (`TARGET_OP(LDIMM)`)

**Fix verified (regression PoC `poc/vm/ldimm-wide-literal.orb` — prints `ALL
TESTS PASSED`, 7 of 7; `ortest` 22/22; `phi-regalloc.orb` green).** The chunk
is widened to `MSize` as it is fetched, so the shift that places it is done in
64 bits:

```cpp
const auto imm = (MSize) FETCH_IMM(instr);
...
REG_N(dst) = shift == 0
                 ? O_TO_SMI(imm)
                 : O_TO_SMI(O_FROM_SMI(REG_N(dst)) | (imm << (16 * shift)));
```

The PoC builds every expected value at run time, so it compares the two paths
rather than two copies of the same literal. Checked by hand as well, against
the edges of the range `LDIMM` serves: `2^32`, `2^48`, the last SMI (`2^61 - 1`,
four chunks), the first value past it (`2^61`, which leaves `LDIMM` for the
constant pool), their negatives and the hexadecimal spellings; all load as
written. The cause had already been confirmed by probing this same change before
the fix landed, and the unfixed decoder fails 6 of the 7 PoC checks.

`ortest/net/02` and `04` pass the 2^32 + 1 bind flag as the literal
`0x100000001` again; while this was open they had to compute it at run time.

<details><summary>Original report</summary>

An integer literal small enough to be an SMI but at least 2^32 loads as the
wrong number, with no diagnostic:

```orb
io.print(4294967295)        // 4294967295   correct
io.print(4294967296)        // 1            expected 4294967296
io.print(1099511627776)     // 256          expected 2^40
io.print(281479271677953)   // 65537        expected 2^48 + 2^32 + 1
io.print(0x100000001)       // 1
io.print(-4294967297)       // -1
```

The same values computed at run time (`65536 * 65536`) are correct, and so are
literals beyond the SMI range (`0x7FFFFFFFFFFFFFFF`), which go through the
constant pool. Only `LDIMM` is involved.

`Builder::LoadImmediate` splits a literal wider than 16 bits into 16-bit
chunks, each an `LDIMM` carrying its chunk and a shift count, and the VM ORs
them together:

```cpp
const auto imm = FETCH_IMM(instr);           // (instr) & 0xFFFFu: a 32-bit unsigned int
...
REG_N(dst) = shift == 0 ? O_TO_SMI(imm) : O_TO_SMI(O_FROM_SMI(REG_N(dst)) | (imm << (16 * shift)));
```

`imm << (16 * shift)` is computed in 32 bits. For the chunk of bits 32-47 the
shift is 32, which is undefined behaviour in C++; for bits 48-63 it is 48. On
ARM64 the hardware takes the count modulo 32, so the third chunk lands on bits
0-15 and the fourth on bits 16-31, which is every observed value above. Other
targets may produce something else again. The encoding and the compiler are
right; the decoder is not.

**Found** while testing the bind flags of `::orbit::net`: a check meant to pass
`0x100000001` (2^32 + 1) received `1`, which is `REUSEADDR`, and the bind
succeeded. `ortest/net/02` and `04` build that value at run time until this is
fixed.

**PoC:** [`poc/vm/ldimm-wide-literal.orb`](poc/vm/ldimm-wide-literal.orb)
*(confirmed live: 6 of 7 checks fail)*. It is expected to fail until this is
fixed, per GUIDE §11.

**Fix (verified by probe, not applied):** widen before shifting,
`((MSize) imm << (16 * shift))`. With that one change the PoC prints `ALL TESTS
PASSED`; the change was then reverted, so the finding stays OPEN.

</details>
---

## VM-004 — A call that fails while binding its arguments leaves them on the stack
**Severity:** High (silent wrong arguments in the enclosing call; unbounded stack growth that ends in a hang) · **Status:** FIXED (2026-10-14) · **Location:** `orbit/orbiter/orbcall.cpp:166` (`ArgumentBinder::Bind`)

**Fix verified (regression PoC `poc/vm/trap-bind-error-argpos.orb` — prints
`ALL TESTS PASSED`, 20 of 20 in 1.1 s; `ortest` 22/22; PoC gate 38/38;
`phi-regalloc.orb` green).** `Bind` now leaves the stack as it found it on every
way out that refuses the call:

- the shared failure path ends at an `ERROR:` label that restores
  `SP = base_sp` (`orbcall.cpp:334`). Every refusal after `base_sp` is fixed goes
  through it: stack exhausted, constructor without its arguments, too many
  arguments (both checks, with and without a spread or curried arguments),
  allocation of the synthesized rest list, named arguments without defaults,
  keyword arguments refused;
- the two refusals before `base_sp` exists pop what the caller pushed
  themselves: a callee that is not callable (`p_count` slots, `orbcall.cpp:175`)
  and a method given a receiver of the wrong type (`stack_args` slots).

The count of the first is right because of how the compiler emits a method
call: the receiver is pushed by `PUSHIF METHOD` only when the target is a
method, and `PUSHIF` itself refuses a target that is not callable before any
argument of the call is pushed. So `Bind` meets a callee that is not callable
only in a call that is not a method call, where `p_count` is exactly what was
pushed. `SPWN`, `DEFER` and `EvalSync` (`runtime.cpp`) bind through the same
function and only `goto ERROR` afterwards, so they get the restore without
popping twice.

The PoC was rewritten to cover every refusal reachable from Orbit code, not
only the one first reported:

- **in argument position**, fourteen shapes: too many arguments (last and
  middle position, many), not callable, wrong receiver, constructor without
  its arguments, too many on a curried function, a spread longer than the gap
  and one past the arity, a named argument without defaults, keyword arguments
  refused (alone, and after defaults and a rest list were pushed), a native
  callee, and a `spawn`; plus two controls;
- **in a loop**, in the frame of the `trap` (a frame that returns resets SP
  anyway and would hide the leak), 150,000 refusals of 32 arguments for each of
  the three ways out (the `ERROR` label, not callable, wrong receiver) and for a
  `defer` caught by a `try` in the same frame. A leak that size is past the
  32 MB stack several times over.

Run against the binder as it was before the fix, all fourteen shapes fail and
the loops hang; with the fix, everything passes. The one refusal not covered is
a stack exhausted inside `Bind` (`EnsureStack`): reaching it takes a recursion
past the point where [VM-007](#vm-007--past-about-510000-frames-every-call-reallocates-the-whole-stack)
makes every frame reallocate the stack.

The hang in the original report was this leak meeting VM-007: leftovers piled
up until the stack entered the region where it grows one request at a time.

Two defects were found while writing the new PoC and are filed on their own:
[VM-008](#vm-008--pushif-reads-a-type-object-as-a-function) and
[IR-010](ir.md).

**Note (2026-10-15):** with VM-007 fixed, the uncovered refusal (a stack
exhausted inside `Bind`) is now waiting on
[VM-009](#vm-009--an-exception-context-dangles-once-the-vm-stack-is-reallocated)
instead: reaching it in the frame of a `trap` needs a `trap` in every frame of
a recursion deep enough to grow the stack.

**Note (2026-10-16):** VM-009 is fixed, so nothing blocks that last refusal
any more; the PoC does not cover it yet.

<details><summary>Original report</summary>

When `ArgumentBinder::Bind` refuses a call, too many positional arguments
being the case reproduced, the arguments already pushed for that call stay on
the stack. Nothing restores SP to `ArgsBaseSP()`. Every other exit path does,
since VM-002, because it leaves through a frame whose prologue records that SP;
a binding failure leaves **before any frame exists**, so that mechanism never
runs. Under `trap` the catch edge resumes in the caller with SP still above the
dead arguments, and two things follow.

**Wrong arguments.** Used as an argument, the trapped call's result is pushed on
top of the leftovers, and the enclosing callee reads its parameters from the
top of the stack:

```orb
func one(a) { return a }
func show(a, b, c) { io.print(a, "|", b, "|", c) }

show("A", "B", trap one(1, 2))      // 1 | 2 | error      expected A | B | error
show("A", trap one(1, 2), "C")      // 2 | error | C      expected A | error | C
```

The corruption reaches further out than the call that holds the `trap`: in the
PoC, the label argument of the `check(...)` wrapping that call is overwritten
too. A native callee fails the same way
(`trap net.parse("1.2.3.4", 1, @INET, 9)`). A failure inside the callee's body
(`trap boom(1)`) and a `trap` in assignment position both read correctly.

**Stack growth.** Assignment position reads correctly but still leaks the slots
on every failure. A loop running `r := trap one(1, 2, 3, 4, 5, 6, 7, 8)` reaches
400,000 iterations in 0.7 s, then **hangs** before 600,000. The same loop with
`trap boom(1)` or `trap one(1)` runs 1,000,000 iterations in under 1 s. That
the process hangs, rather than reporting a stack overflow, may be a defect of
its own and is worth checking once the leak is gone.

**Found** while writing `ortest/net/05_udp_datagrams.orb`:
`io.print("label", trap h.send(b"x", addr))` printed the address where the label
should have been (`to` is keyword-only, so the call fails in the binder).

**PoC:** [`poc/vm/trap-bind-error-argpos.orb`](poc/vm/trap-bind-error-argpos.orb)
*(confirmed live: 4 of 6 checks fail; the two controls pass)*. It is expected to
fail until this is fixed, per GUIDE §11.

**Fix (suggested, not tried):** on `CallResult::ERROR` from `Bind`, restore SP
to `binder.ArgsBaseSP()` before `goto ERROR`, so that a refused call leaves the
stack as it found it, which is what a call that returns already does. Check the
other callers of `Bind` (`SPWN`, `DEFER`, `runtime.cpp:536`) for the same
pattern, and whether the callee slot and any receiver below `ArgsBaseSP()` need
popping too.

## VM-003 — `break` out of a `try` block is silently ignored
**Severity:** High (silent wrong control flow, no diagnostic) · **Status:** FIXED (2026-10-05) · **Location:** `orbit/liftoff/ir/jblock.cpp:94` (`FindLabeledBlock`), `orbit/orbiter/vm.cpp` (`TARGET_OP(TEND)`)

**Fix verified (regression PoC `poc/vm/break-out-of-try.orb` and the new
`ortest/exceptions/01_abrupt_exit.orb`, both green; each fix mutation-tested by
restoring the old line and watching the suite fail).**

The cause was in the compiler, not in the VM, and the original report's reading
of it was wrong: the machinery was not "in place with only the last step
missing", it was never invoked. `FindLabeledBlock` resolved an unlabelled target
to the innermost jump block that was not `SYNC`, which for a jump inside a `try`
is the TCF block. Two consequences followed, and together they account for the
symptom exactly:

- `GetJBlockEnd` accepts a TCF block and returns the **end of the try**, so the
  `break` left the try and the loop carried on to the next iteration;
- the walk in `visitJump` looking for interposed try blocks is
  `for (cursor = j_chain; cursor != b_target; ...)`, and with `b_target` already
  the head of the chain its body never ran. **No `TSPA` was emitted at all**, so
  the pending-action path in `TEND` was dead code for this shape.

The unlabelled case now returns only a `LOOP` or a `FOR_IN`, which is the rule
the labelled branch of the same function already used.

Two further defects surfaced once the jumps started reaching the VM, both fixed
in the same pass:

- **the propagation in `TEND` re-entered its own finally.** When an inner try
  completed abruptly and the outer context already carried an action, the outer
  context's finally is the block being executed, and `JMP_TO(outer->finally_off)`
  restarted it forever. The action pending on the outer context is now read
  before being overwritten, and the jump is taken only when there was none.
- **`ExPtrContainer::SetAction` accumulated instead of replacing.** The actions
  are packed two bits per context, so OR-ing two of them produces a third
  (`BREAK | CONTINUE == RETURN`). Reachable precisely once a finally can complete
  abruptly while another action is pending.

The resulting semantics, pinned down by the suite: an abrupt completion inside a
finally **replaces** the pending one, as in Java. `break` in a finally over a
pending `return` breaks; `return` in a finally over a pending `break` returns.

[IR-008](ir.md) was the `continue` spelling of the same root cause and is closed
with it. [IR-009](ir.md) was found while validating this and is unrelated: the
reproducer that exposed it contains no jump.

<details><summary>Original report</summary>

A `break` whose loop is outside the enclosing `try` does not leave the loop. It
raises nothing and prints nothing: the loop simply runs to completion.

```orb
var n = 0
for var i in [1, 2, 3, 4, 5] {
    try {
        if i == 3 { break }
        n += 1
    } catch e {
        n += 100
    }
}
// n == 4, expected 2
```

The machinery is in place and only the last step misses. `irbuilder` emits a
`TSPA` with `PendingAction::BREAK` and packs the target block's offset into the
operand (`GetJBlockEnd`), `TEND` reads the pending action and does
`JMP_TO(target)` with `target = ctx->ret_pops`. That jump lands somewhere that
resumes the loop instead of leaving it.

**Not a regression** from the `ExceptionContext::SP` work: verified by building
both revisions and running the reproducer above, which prints `n = 4` on each.
Note, however, that the writes to `ret_pops` are now commented out (see the
**Dead field** note under [VM-002](#vm-002--a-deferred-control-transfer-in-argument-position-overwrites-the-enclosing-calls-arguments)),
so whoever fixes this has to restore a way to carry the target offset first: the
value `TEND` reads today is whatever the stack slot last held.

**PoC:** none filed; the ten lines above reproduce it directly *(confirmed
live)*. A PoC belongs here once the fix lands, together with the `continue`
half tracked as [IR-008](ir.md).

**Fix:** carry the `TSPA` operand through to `TEND` again (a field of its own
rather than the overloaded `ret_pops`), and confirm the offset it jumps to is
the loop's exit block. See also IR-008: the `continue` spelling of the same
construct does not even compile.

</details>

</details>
---

## VM-002 — A deferred control transfer in argument position overwrites the enclosing call's arguments
**Severity:** High (silent wrong arguments; writes below the argument region when the inner call has more arguments than the outer has pushed) · **Status:** FIXED (2026-10-01) · **Location:** `orbit/orbiter/fiber.cpp` (`PushState`, `PopStateNoCtxRestore`)

**Fix verified (regression PoC `poc/vm/trap-argpos-clobber.orb` — prints `ALL
TESTS PASSED`; `ortest` 19/19).** Both halves went away at once, and neither was
fixed where it was reported: the cure was to stop deriving the stack pointer on
the way out of a frame and start recording it on the way in.

The call frame prologue gained a fourth word. `PushState` now takes the bottom
of the argument region and saves it alongside BP and IP (`Context | SP | BP |
IP`), and `PopStateNoCtxRestore` reads it back. Returning from a frame therefore
restores SP to exactly where the caller was before it pushed anything, on every
exit path there is, and no path has to carry a count any more:

- the **trapped panic** half: `UnwindStack` walks frames through that same
  function, so the catch edge lands on the right SP without touching it;
- the **`return` through a `finally`** half: the deferred `RET` at
  `TARGET_OP(TEND)` calls `Return`, which calls `PopState`, which now restores
  the recorded SP. The `pops` it is handed is ignored, which is just as well:
  the original code read `ctx->ret_pops` *after* `ReleaseExceptionContext` had
  zeroed the context, so it had always been passing 0. That read-after-clear was
  the true root cause of this half, and it is now moot.

The value saved is `ArgumentBinder::ArgsBaseSP()`, fixed inside `Bind` right
after receiver normalization and before the binder pushes anything itself. It is
deliberately not derived from the bound argument count: the binder pushes slots
it does not count (the rest list, the kwargs dict) and drops the receiver of a
method-mode call whose callee is not a method, because that slot belongs to the
caller. Both discrepancies were found by this work and are what made a
count-based cleanup unfixable in general.

The `pops` parameter of `Return`, the 16-bit POP VALUES field of `RET`, the
RETURN operand of `TSPA` and `visitReturn`'s `pops_slot` are all dead as a
result, and `ExceptionContext::ret_pops` now carries only the BREAK/CONTINUE
jump target (see [VM-003](#vm-003--break-out-of-a-try-block-is-silently-ignored)).

<details><summary>Original report (trapped panics)</summary>

When a `trap` expression is passed **as an argument to a call** and the trapped
expression **panics**, the panicking call's own arguments end up in the slots
the enclosing call has already pushed. The outer callee runs with values it was
never passed.

```orb
func boom2(x, y) { panic Error(@X, "no") }
func show(a, b, c, d) { io.print(a, b, c, d) }

show("A", "B", trap boom2(10, 20), "D")    // a=10  b=20  c=error  d="D"
```

The damage scales with the arity of the trapped call, and is positional rather
than a clean overwrite (`seen4` prints `a|b|d`):

| trapped call | expected | observed |
|---|---|---|
| `trap boom1(11)` | `A\|B\|D` | `B\|11\|D` |
| `trap boom2(10, 20)` | `A\|B\|D` | `10\|20\|D` |
| `trap boom3(1, 2, 3)` | `A\|B\|D` | `2\|3\|D` |

The last row is the reason for the severity: two arguments had been pushed and
three were written over them, so one write landed **below** the enclosing call's
argument region, into whatever the frame keeps there. The PoC survives it, which
is luck, not safety.

Three controls narrow it down, and all three are green:

- the identical shape with a call that **returns normally** (`trap sum2(10, 20)`)
  binds every argument correctly, so this is the catch edge and not the
  argument-push sequence by itself;
- `trap` over a non-call, and a plain nested call in the same position, are both
  correct;
- `trap` in **assignment** position (`r = trap boom2(10, 20)`, the form every
  existing suite uses) is correct, which is why `ortest/calls/01_argument_passing.orb`
  has never caught it: it only ever traps into a variable.

**Hypothesis** (not confirmed against the bytecode): on the catch edge SP is not
restored to the enclosing call's argument cursor, so the inner call's pushes are
laid down from the wrong base. Same family as VM-001, which was the previous
SP-restoration defect on this path; the fix there deliberately stopped touching
SP at all, and an argument cursor belonging to a call that is still being built
is a case that fix does not cover.

**PoC:** [`poc/vm/trap-argpos-clobber.orb`](poc/vm/trap-argpos-clobber.orb)
*(confirmed live — 3 of 8 checks fail; it is expected to fail until this is
fixed, per GUIDE §11)*. Found while writing `ortest/net/`, where every refusal
check had the shape `check_error(name, trap sock.recv(-1), kind, substr)` and
the failing test printed a corrupted label; all 41 sites there were rewritten to
hoist the `trap` into a variable first.

**Fix:** restore SP on the catch edge to the cursor of the call currently being
assembled, not to the frame's base, so the arguments already pushed for it stay
reserved. Then drop the hoisting workaround in `ortest/net/` and let those
suites use `trap` inline again.

</details>

---

## VM-001 — Spill slots clobbered after a trapped panic (SP rewound to end of exception block)
**Severity:** High (silent wrong-value reads after any `trap` that actually catches) · **Status:** FIXED (2026-07-08) · **Location:** `orbit/orbiter/vm.cpp` (`UnwindStack`, catch branch)

**Fix verified (regression PoC `poc/vm/trap-unwind-sp.orb` — prints `ALL TESTS
PASSED`).** Root cause: on catch, `UnwindStack` rewound SP to the **end of the
ExceptionContext block**:

```cpp
regs->SP.reg = ((unsigned char *) (regs->CP.reg + sizeof(ExceptionContext))) - stack->stack;
```

a leftover of the old trap system, which assumed the exception block was the
top of the frame. LinearScan spill slots are allocated in the prologue *above*
the exception block (`GetFreeStackSlot` keeps counting past the
`StackSlotGuard` words), so the first SP-relative `PUSH` after the catch wrote
its argument straight into a BP-relative spill slot; the next `SKLDR` then
reloaded the pushed value (a String/Bytes) instead of the spilled one (e.g. a
global NativeFunc) — hence "invalid call to a non-callable object" / wrong
member lookups on the second call after a caught panic.

The fix is to **not touch SP at all**: the frame-pop loop above has already
left SP at the caller's value at call time (base of the saved `FiberContext`),
i.e. with the entire prologue — exception block and every slot allocated after
it — still reserved. The trap block lives in the frame prologue and stays
reserved for as long as the frame exists. LinearScan needed no change (the
same bytecode ran correctly whenever the panic did not fire).

<details><summary>Original report</summary>

**Reproducer (minimal):**

```orb
import "io"

func boom() { panic Error(@X, "no") }

e := trap boom()

io.print("first")     # ok
io.print("second")    # AttributeError: 'Bytes' object has no property 'print'
```

After a panic is **caught** by `trap`, the *second* global access that follows
reads a stale value: `io` resolves to a leftover `Bytes` (plausibly
`io.print`'s own argument-conversion buffer from the first call). Variants:

```orb
# any callable global after the trap:
func hello() { return "hello" }
e := trap boom()
io.print(hello())   # TypeError: invalid call to a non-callable object('String')
```

- A single global access after the trap is fine; it's from the second one on
  that values are wrong.
- `trap` over a call that does **not** panic is fine (baseline passes).
- The panicking function needs no arguments; any trapped panic triggers it.
- First observed as `regex.Pattern.replace` tests failing *after* an earlier
  `trap p.replace(...)` TypeError test in the same script.
- A spurious extra newline is printed by the first `io.print` after the trap —
  corruption is already present at that point, it just doesn't crash yet.

**Initial hypothesis** (superseded by the confirmed root cause above): the
exception edge was suspected of leaving the register *file* inconsistent with
the allocator's model. In reality the registers and the reloads were fine — it
was the spill slots' *memory* being overwritten by post-catch PUSHes, because
of the SP rewind. Related history: `Fiber::PopState` SP leak (already fixed)
lived on this same unwind path.

</details>
