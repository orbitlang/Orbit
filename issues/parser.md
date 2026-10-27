# parser — bug report

**Component:** `orbit/liftoff/parser` (parser.cpp, parser.h, context.h, ast.h) · **ID prefix:** `PARSE`
**PoCs:** [`poc/parser/`](poc/parser/) · **Last reviewed:** 2026-08-31
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

> Findings marked **[confirmed]** were reproduced against the current
> `bin/Orbit` binary.

---

## PARSE-027 — A short declaration that shadows a name is never read: later uses reach the outer variable
**Severity:** High (silent wrong result; writes land on the outer variable) · **Status:** OPEN · **Location:** `orbit/liftoff/parser/parser.cpp:2283` (`ParseWalrus`), `orbit/liftoff/symtable.cpp:56` (`SymbolNew`), `:573` (`Lookup`), `:373` (`LeaveMergeNestedScope`)

A short declaration of a name that an enclosing scope already has declares a
new variable that no later use ever finds. Reads and writes resolve to the
outer variable:

```orbit
x := 1
func f() {
    x := 2
    x += 1
    return x      // 2: reads the module's x, which `x += 1` changed from 1 to 2
}
```

The same happens with the tuple form (`a, b := (5, 6)`), with a block in a
function (`y := 2` inside an `if`, where the function has a `y`) and with a
nested function (`z := 2`, where the enclosing function has a `z`). With
`var` instead of `:=` every case is right.

`ParseWalrus` declares the symbol with `SymbolType::VARIABLE` and never sets
`SymbolFlags::INITIALIZED` on it (`ParseVarDecl` sets it once the value is
parsed, `parser.cpp:1034`), and `Lookup` skips a symbol without the flag.
When the name is new the problem is hidden: the identifier on the left was
first parsed as an expression, which inserted an UNKNOWN placeholder at the
same offset, and `SymbolNew` gives every non-VARIABLE symbol the flag; the
declaration reuses that placeholder and keeps it. When an enclosing scope has
the name, `LookupInsert` returns the outer symbol instead, the declaration
creates a fresh one without the flag, and every use after it skips it.

In a `when`, the flag is what `LeaveMergeNestedScope` copies onto the module
slot it publishes (`canonical->flags = sym->flags`). The second branch's `:=`
finds the slot published by the first, so it declares a fresh symbol, and its
merge clears the slot's flag. The first use after the `when` then fails to
find the slot, tries to insert a placeholder in the module and stops the
compile with "symbol already exists":

```orbit
when false {
    w := "first"
} else {
    w := "second"
}
io.print(w)       // symbol already exists
```

**Found** while writing the suite for `when` blocks restricted to
declarations: a nested `when` declaring the same name with `:=` in both
branches was refused.

**PoC:** [`poc/parser/parse-027.orb`](poc/parser/parse-027.orb) *(confirmed
live: the file is refused because of its `when` check; without it, the two
controls pass and the six shadowing checks fail)*.

**Fix (suggested, verified by probe 2026-10-27, then reverted):** mark the
declared symbols INITIALIZED in `ParseWalrus` once the value is parsed:
`sym->flags |= SymbolFlags::INITIALIZED` after the single-name `Declare`, and
for the tuple form a loop over the `VAR_DECLARATIONS` elements after
`decl->value` is parsed (setting the flag there, not when the tuple is
declared before the value, keeps the right-hand side reading the outer names:
see PARSE-010). With the probe the PoC prints `ALL TESTS PASSED`, `ortest` is
34/34, the PoC gate is unchanged (63/64 with the open VM-010, the PoC not
counted) and `phi-regalloc.orb` passes.

---

## PARSE-026 — A bodiless method ending in `async`/`const` swallows the next member
**Severity:** Medium (valid source refused) · **Status:** FIXED (2026-10-27) · **Location:** `orbit/liftoff/parser/parser.cpp:2396` (`ParseFunction`, the trailing-modifier loop)

**Fix verified (live: the PoC passes; it fails again with the old
`MatchEat(KW_ASYNC, true)` put back, and with the newline skipped only before
the loop; `ortest` 31/31, the PoC gate 61/64 with the open IR-018, IR-019 and
VM-010; every file under `stdlib/` parses).** The loop reads a trailing
modifier without the newline after it, and skips a newline only when the next
line starts with another modifier:

```cpp
while (true) {
    this->IgnoreNewLineIF(TokenType::KW_ASYNC);
    this->IgnoreNewLineIF(TokenType::KW_CONST);

    if (TKCUR_TYPE <= TokenType::KEYWORD_BEGIN || TKCUR_TYPE >= TokenType::KEYWORD_END)
        break;
    ...
    if (this->MatchEat(TokenType::KW_ASYNC, false))
```

so a newline followed by anything else ends the signature, and the next member
is parsed on its own. Every modifier may start a line of its own, as the first
one already could, and the newline before a body's `{` is still skipped. Also
checked with `orbit-astdump`: `async const` on one line, each modifier on a
line of its own (with and without a body, in both orders), a bodiless method
followed by one with no modifiers, and a body whose `{` is on the next line
all parse, and each method ends with its last modifier or its `}`. The PoC
covers modifiers on lines of their own as well.

<details><summary>Original report</summary>

```orbit
trait Job {
    pub func run() async
    pub func name() { return "job" }
}
```

is refused with "Invalid syntax: unexpected token or expression". The loop
that reads the trailing modifiers consumes `async` / `const` with
`MatchEat(..., true)`, which also eats the newline. The next token is then the
following member's `pub` or `func`, a keyword, and the loop takes it for one
more modifier and throws. A method with a body is not affected, since its `{`
stops the loop first.

**Found** while checking that a bodiless method now ends with its signature
(PARSE-019's audit); the loop is older than that change.

**PoC:** [`poc/parser/parse-026.orb`](poc/parser/parse-026.orb) *(confirmed
live: the file is refused)*.

**Fix (suggested, not tried):** consume a trailing modifier without the
newline (`MatchEat(..., false)`), with `IgnoreNewLineIF` before each further
one as the code already does before the loop, so a newline ends the
signature.

</details>

---

## PARSE-025 — `init`/`cleanup` docstrings are silently dropped
**Severity:** Low (docs only) · **Status:** OPEN · **Location:** `parser.cpp:2567-2571` (`Eat`) vs `parser.cpp:1173` (`ParseCleanupInit` → `GetDocString`)

`Eat` keeps a pending `/** */` doc comment (`doc_`) only when the next token is
in a whitelist; otherwise it discards it:

```c
} else if (this->doc_.type == TokenType::COMMENT_DOC
           && !this->Match(TokenType::DECORATOR, TokenType::KW_CLASS, TokenType::KW_NATIVE,
                           TokenType::KW_TRAIT, TokenType::KW_FUNC, TokenType::SEMICOLON,
                           TokenType::END_OF_LINE, TokenType::KW_PROT, TokenType::KW_PUB))
    this->doc_.~Token();
```

`KW_INIT` and `KW_CLEANUP` are **not** in that list, so a doc comment placed
before a constructor/destructor is discarded the moment `Eat` reaches the
`init`/`cleanup` keyword. Yet `ParseCleanupInit` then *does* try to read it:

```c
func->doc = this->GetDocString(false).release();   // always {} for init/cleanup
```

By that point `doc_` has been destroyed, so `GetDocString` returns `{}` and the
constructor/destructor docstring is silently lost. No crash (the discarded
`doc_` is null, and `GetDocString` handles that), just missing documentation —
inconsistent with `func`/`class`/`trait`/`native`, which retain their docs.

**PoC (by inspection):**
```
class C {
    /** @brief Build a C. */
    init() { }
}
```
`init`'s `doc` ends up empty.

**Fix:** add `TokenType::KW_INIT` and `TokenType::KW_CLEANUP` to the retention
whitelist in `Eat`, so a doc comment survives until `ParseCleanupInit` consumes
it.

---

## PARSE-024 — `IO_ERROR` (and other non-NOMEM scanner errors) miscategorized as SYNTAX
**Severity:** Info · **Status:** OPEN · **Location:** `parser.cpp:2592-2598` (`Parse`, `catch (ScannerException&)`)

```c
} catch (ScannerException &) {
    this->error_.type = ParserErrorType::SYNTAX;
    if (this->scanner_.status == ScannerStatus::NOMEM)
        this->error_.type = ParserErrorType::NOMEM;
    this->error_.message = this->scanner_.GetStatusMessage();
}
```

The handler classifies every scanner failure as `SYNTAX` unless the status is
exactly `NOMEM`. Non-syntactic scanner errors therefore get the wrong
`ParserErrorType`:

- `ScannerStatus::IO_ERROR` (input/output error while reading source — added
  with IBUF-004) is reported as a **syntax error**, though nothing about the
  source is malformed. `GENERIC_ERROR` (or a dedicated category) would be
  truthful.

The *message* is correct (via `GetStatusMessage`); only the `type`
categorization is wrong, which matters for callers that branch on
`ParserErrorType` (exit codes, retry, diagnostics formatting).

**Fix:** map scanner statuses to error types explicitly instead of
"SYNTAX-unless-NOMEM" — e.g. `IO_ERROR → GENERIC_ERROR`, `NOMEM → NOMEM`,
everything else → `SYNTAX`. Best folded into the planned error-reporting rework.

**Note:** deferred at the author's request — part of a broader error-reporting
overhaul planned later.

---

## PARSE-023 — Anonymous function names collide across contexts
**Severity:** Info · **Status:** OPEN · **Location:** `parser.cpp:2398-2404` + `context.h:28`

`MakeFuncName` uses `context_->anon_count++`, but `anon_count` lives on each
`Context` and every function/class pushes a fresh one — two anonymous
functions in different enclosing functions are both named `func$0`. Fine while
names are scope-local; a problem if codegen ever assumes global uniqueness
(e.g. symbol dumps, debug info).

---

## PARSE-022 — Synthetic `init` skips the super.init requirement
**Severity:** Info (verify against codegen) · **Status:** OPEN · **Location:** `parser.cpp:2469-2491` (`ClassCheck`)

For derived classes with an explicit `init`, the first statement must be
`super.init(...)` (error 80). If the class has *no* init, `InjectInit`
synthesizes an empty constructor that does **not** call `super.init` — and no
error is raised. Either the synthetic ctor should inject the super call, or
codegen must handle implicit super-init (worth verifying).

---

## PARSE-021 — `f(x=)` and `func f(a=)`: empty default/named value accepted
**Severity:** Info · **Status:** OPEN · **Location:** `parser.cpp:1451-1455, 2209-2212`

Both named-argument and named-parameter parsing make the value optional
(`if (!Match(COMMA, RIGHT_ROUND)) value = …`), so a dangling `=` produces a
named arg/param with `value == nullptr`. If "named without value" isn't a
deliberate feature, this should be a syntax error.

---

## PARSE-020 — Meaningless access modifiers silently accepted
**Severity:** Info · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:1971-2003, 2059-2062` (`ParseStatement`)

**Fix verified (confirmed live; PoC `poc/parser/parse-020.orb`).** Modifiers are now
validated instead of parsed-and-ignored: duplicate `pub pub`/`weak weak` (98),
`prot` outside class/trait (81), `weak` outside a class (25), a modifier before a
decorator (0, see PARSE-007), and a modifier on a labeled statement (99/100) all
diagnose. A statement that cannot carry a modifier rejects it ("…can only be
applied to declarations…"): `pub if x {}`, `pub return`, `weak func` now error.

Note: `pub import "io"` — listed in the original report — is in fact **valid** (a
public re-export is a real declaration) and stays accepted.

<details><summary>Original report</summary>

`pub if x {}`, `pub import "io"`, `pub return`, `weak func …`, `pub pub var x`
— modifiers are parsed and then ignored by statement branches that don't use
`access`/`weak`, with no diagnostic. Reject modifiers when the following
statement can't carry them.

</details>

---

## PARSE-019 — Location/scope offsets use the token *after* the construct
**Severity:** Info (diagnostics/scopes) · **Status:** FIXED (2026-10-27) · **Location:** `orbit/liftoff/parser/parser.cpp` (every `Parse*` function that builds a location or closes a scope)

**Fix verified (`ortest` 31/31, the PoC gate 60/64 with the four open
findings IR-018, IR-019, PARSE-026 and VM-010, every file under `stdlib/`
compiles; `phi-regalloc.orb` green).** The whole parser follows one rule,
`ParseBlock`'s: a node's `loc.end` is the end of its last token, read before
consuming it, or the `loc.end` of its last child; a scope tied to a node closes
at that node's `loc.end.offset`; and a declaration's location starts where its
symbol is declared. The three original points and every point of the audit
below are fixed:

- **scopes:** blocks (`ParseBlock`, IR-010), switch cases (whole-case scope,
  IR-017), `when` branches, the synthesized `init` (no `+1`), bodiless methods
  (end at their signature);
- **locations:** a call ends with its `)`; `var x` with no value ends with its
  last name; `native var` has an end; a `native from` block starts at `native`
  and ends with its `}`, and its functions start at `func`; a decorator ends
  with its function; `break` / `continue` include their label; an imported
  name includes its alias; the for-in variable starts at `var`; the module
  spans its statements;
- **access modifiers** are part of the declaration for every kind (`pub
  class`, `pub trait`, `pub import`, `pub x := 1`, as `pub func` already was).
  For classes, traits and imports the symbol is declared at that start too:
  `visitConstruct` looks a class up by its node's start offset, and a name is
  visible only after its declaration, so moving the node's start alone broke
  every `pub class` (seen while fixing: the whole of `stdlib/io` stopped
  compiling).

Found and filed during the audit: IR-017 (fixed), IR-019 and PARSE-026. There
is no tool to print the AST with its locations, so the location-only points
are verified by reading and by the gates, not by inspecting the positions.

<details><summary>Original report and audit</summary>

**First point fixed (2026-10-27), with IR-010.** `ParseBlock` sets
`block->loc.end` to the end of the `}` (`TKCUR_END`) before consuming it and
closes the scope at that offset, so a block's location and its scope both end
with its `}`; adjacent blocks no longer overlap. `ParseTryCatchFinally` now also sets
the end of the whole try statement. Verified through the IR-010 PoC and the
gates. The two other points below (`ParseSwitchCase`, `ParseForInStatement`)
are untouched.

- `ParseBlock`: `block->loc.end = TKCUR_START` and
  `LeaveNestedScope(TKCUR_END.offset)` run *after* `MatchEat(RIGHT_BRACES)`,
  i.e. they reference the token following `}` — block ranges/scope boundaries
  extend one token too far.
- `ParseSwitchCase`: `block->loc.end` is never updated, so case loc.end points
  at the colon area.
- `ParseForInStatement` passes `constexpr Position end{}` as the var-decl
  start → `{0,0,0}` start location.


**Note (2026-10-14):** no longer only cosmetic. The try block's scope covering
the `catch` keyword makes the IR builder enter it for the catch body, and any
block nested there aborts the compiler: see [IR-010](ir.md).

**Note (2026-10-27):** the second point is not cosmetic either. The case's
scope is closed at that stale `loc.end`, so it covers only the first token
after the colon, and an `if` in a case aborts the compiler: see
[IR-017](ir.md).

**Full audit (2026-10-27, by inspection of every `Parse*` function).** The
reference is `ParseBlock`: a node's `loc.end` is the end (`TKCUR_END`) of its
last token, read before consuming it, or the `loc.end` of its last child; a
scope tied to a node closes at that node's `loc.end.offset`. Departures found,
the ones that move a scope first:

*Scopes (all four fixed 2026-10-27):*
- ~~`ParseSwitchCase`: the case block's `loc.end` is never updated, and the
  case scope closes there (IR-017).~~ The case ends with its last statement
  (or `fallthrough`), or with the colon when the body is empty, and its scope
  spans the whole case, from `case` / `default`, so it is never empty; the IR
  enters it through the case's first token. See IR-017.
- ~~`ParseWhenStatement`: both the `when` and the `else` scopes close at
  `TKCUR_START`, the token after the `}`.~~ They close at their block's
  `loc.end.offset`. (The IR still does not enter them: IR-019.)
- ~~`InjectInit`: `loc.end.offset += 1` compensated for blocks that used to
  end at the start of their `}`, and its node took the token after the
  class.~~ The synthesized `init` takes the class's location and closes its
  scope where the class's ends.
- ~~`ParseFunction`: a method without a body ends at `TKCUR_START`, the token
  after its signature.~~ It ends with the last token of its signature: the
  `)`, or a trailing `async` / `const`. (A trailing modifier still swallows
  the next member: PARSE-026.)

*Locations only:*
- `ParseFuncCall` (`:1609`): a call ends with its last argument, not with its
  `)`; `f()` ends with the `(`.
- `ParseVarDecl` (`:975`): with no value (`var x`, and the `var` of a for-in),
  the declaration ends with the token after the names (`=`, a newline, `in`).
- `ParseNativeVarStatement` (`:660`): `loc.end` is never set; it stays the end
  of `var` / `let`.
- `ParseNativeBlock` (`:479`, `:517`): starts at the module name instead of
  `native`, and ends at the start of its `}`; the functions declared inside
  it start at their name (`:497`) where a standalone one starts at `native`.
- `ParseDecorator` (`:199`): `loc.end` is never set; it should be the
  decorated function's.
- `ParseStatement`, `break` / `continue` (`:2063`): a label (`continue
  outer`) is not part of the jump's location.
- `ParseImportStatement` (`:387`): an imported name with an alias
  (`a as b`) ends at `a`.
- `ParseForInStatement` (`:282`): the loop variable's declaration starts at
  `{0, 0, 0}` (`constexpr Position end{}` passed as its start).
- Access modifiers: `pub func`, `pub var`, `pub init` start at the modifier,
  `pub class`, `pub trait`, `pub import` and `pub x := 1` at the keyword or
  name after it.
- `Parse` (`:2766`): the module node keeps the location of its first token;
  the computed range is used only to close the module scope.

</details>

---

## PARSE-018 — Wrong error index for invalid label target
**Severity:** Low (diagnostics) · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:2149-2150`

**Fix verified (confirmed live; PoC `poc/parser/parse-018.orb`).** A non-identifier
label target now throws the dedicated label error:

```c
if (stmt->node_type != NodeType::IDENTIFIER)
    throw ParserException(97);
```

`a + b: loop {…}` now reports "only an identifier can be used as a label" instead
of the catch-clause message (index 33). The surrounding label rework also gives
duplicate-label (34), non-loop-target (35) and modifier-on-label (99/100) their
own diagnostics.

<details><summary>Original report</summary>

`if (stmt->node_type != NodeType::IDENTIFIER) throw ParserException(33);` —
index 33 is *"Invalid catch clause: expected @atom after catch"*. A statement
like `a + b: loop {…}` reports a catch-clause error. Should be a dedicated
message (34/35 family).

</details>

---

## PARSE-017 — `!func->constant` check is always true when self is added
**Severity:** Low (suspicious logic) · **Status:** OPEN · **Location:** `parser.cpp:2129-2136` (`ParseFunction`)

The `self` parameter is added when `!func->constant && CheckBack(CLASS|TRAIT)`
— but `func->constant` is only set *later* (the `const` qualifier is parsed
after the parameter list), so the condition is always true at this point.
Either `const` methods are supposed to skip `self` (bug: they don't) or the
check is dead and should be removed.

---

## PARSE-016 — `isalnum`/`isalpha` on signed `char` (UB for non-ASCII paths)
**Severity:** Low · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:2624, 2627` (`CheckSetImportAlias`)

**Fix verified (by inspection).** The auto-alias scan now casts each path byte to
`unsigned char` before the ctype calls:

```c
while (idx < length && std::isalnum((unsigned char) *((mod_name_end - idx) - 1)))
...
if (!std::isalpha((unsigned char) *(mod_name_end - idx)))
```

so UTF-8 path bytes ≥ 0x80 (negative `char` on arm64/x86_64) no longer feed a
negative value to `isalnum`/`isalpha` (UB).

<details><summary>Original report</summary>

The auto-alias scan feeds raw `char` values from the module path into
`isalnum`/`isalpha`. For UTF-8 path bytes ≥ 0x80, `char` is negative on
macOS/arm64 and passing negative values (other than EOF) to ctype functions is
UB. Cast to `unsigned char` first.

</details>

---

## PARSE-015 — Inconsistent OOM checks on `ORStringNew(...).release()`
**Severity:** Low (OOM paths) · **Status:** OPEN · **Location:** `parser.cpp:421, 503, 548, 564, 578, 651`

Some `ORStringNew` results are null-checked (`ct->name`, `imp_name->name`,
`bc->label`…), these are not: `imp->path` (IMPORT_FROM), `native_name` ×2,
`np->name`, `mod_name` ×2. On allocation failure the AST silently carries a
null field into later phases instead of throwing `DatatypeException`.

---

## PARSE-014 — Unchecked `Lookup(...)->type` on label declaration
**Severity:** Low · **Status:** OPEN · **Location:** `parser.cpp:1981`

```c
this->sym_t_->Lookup(...)->type = SymbolType::LABEL;
```
`Lookup` can return `nullptr` (it does in other call sites that all check).
Today the symbol always exists because `ParseIdentifier` did a `LookupInsert`
moments earlier, but the deref is one refactor away from a crash.

---

## PARSE-013 — `Parse()` is `noexcept` but std containers can throw
**Severity:** Low · **Status:** OPEN · **Location:** `parser.cpp:2544`

The catch clauses cover only project exceptions. Any `std::bad_alloc` from the
many `std::vector::push_back`s (or `std::length_error`) escapes a `noexcept`
function → `std::terminate` instead of a NOMEM `ParserError`.

**Fix:** add `catch (std::bad_alloc &)` → NOMEM, and a defensive
`catch (...)` → GENERIC_ERROR.

---

## PARSE-012 — Char literals decoded via broken `StringUTF8ToInt`
**Severity:** Low · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:1796-1800` (`ParseLiteral`, NUMBER_CHR)

**Fix verified (confirmed live; PoC `poc/parser/parse-012.orb`).** The NUMBER_CHR case
now validates the buffer is exactly one well-formed code point before decoding:

```c
if (!StringUTF8IsSingleCodePoint(this->tkcur_.buffer, this->tkcur_.length))
    throw ParserException(96);
handle = UIntNew(this->isolate_, StringUTF8ToInt(this->tkcur_.buffer));
```

A multi-byte or invalid buffer (e.g. `'\q'` preserved as `\`,`q`) is now rejected
("expected a single valid Unicode code point") instead of silently truncating or
producing a huge value.

<details><summary>Original report</summary>

`UIntNew(isolate_, StringUTF8ToInt(tkcur_.buffer))`:
- For a byte ≥ 0xF1 (e.g. `'\xFF'`) `StringUTF8ToInt` returns **-1**
  (see UTF8-002), which silently becomes a huge unsigned value.
- A buffer holding a scanner-preserved invalid escape (e.g. `'\q'` → bytes
  `\`,`q`, see SCAN-011) is silently truncated to the first byte: the char
  literal evaluates to `0x5C` (`'\'`).

**Fix:** validate the buffer is exactly one well-formed codepoint and reject
otherwise; fix UTF8-002 first.

</details>

---

## PARSE-011 — `ParsePrefix`: self-assignment instead of propagating loc
**Severity:** Low · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:1958`

**Fix verified (by inspection).** The line now propagates the operand's end into
the prefix node:

```c
prefix->loc.end = prefix->value->loc.end;
```

so unary expressions (`-x`, `!x`, `~x`, `<-ch`…) carry the full source range
instead of stopping at the operator token.

<details><summary>Original report</summary>

```c
prefix->value->loc.end = prefix->value->loc.end;   // no-op
```
Intended: `prefix->loc.end = prefix->value->loc.end;`. Every unary expression
(`-x`, `!x`, `~x`, `<-ch`…) keeps `loc.end` at the operator token —
wrong ranges in diagnostics.

</details>

---

## PARSE-010 — Walrus shadowing differs between single and tuple form
**Severity:** Medium (semantics) · **Status:** OPEN · **Location:** `parser.cpp:2044-2083` (`ParseWalrus`)

Tuple form declares the LHS symbols **before** parsing the RHS; single form
declares the LHS **after**. So in `x := x` the RHS resolves to the outer `x`,
while in `x, y := x, 1` the RHS `x` resolves to the *freshly declared* (still
uninitialized) `x`. One of the two is wrong, presumably the tuple form.

**Fix:** declare tuple symbols after `decl->value` is parsed, like the single
case.

---

## PARSE-009 — Manual destructor calls on `doc_` (UB, works by accident)
**Severity:** Medium (latent) · **Status:** OPEN · **Location:** `parser.cpp:2509-2519` (`Eat`)

```c
this->doc_.~Token();
this->doc_ = std::move(this->tkcur_);   // assignment to a destroyed object
...
this->doc_.~Token();                    // object left destroyed, read & destroyed again later
```

Two problems stacked:
1. Explicitly destroying `doc_` and then assigning to / reading from it is UB;
   it "works" only because `~Token()` nulls `buffer` and the move-assign
   overwrites every field. The second call site (line 2518) leaves `doc_`
   destroyed; its destructor will run again at parser teardown.
2. The comment-skip loop condition (`TokenInRange(COMMENT_BEGIN, COMMENT_END)`)
   then reads `tkcur_.type` from a *moved-from* token and relies on the move
   ctor not resetting `type` (see SCAN-017) to keep iterating.

The manual destructor exists only because `Token::operator=` doesn't free the
destination buffer.

**Fix:** give `Token` a proper move-assign that releases the destination
buffer (and resets `other.type`), then replace both `~Token()` calls with
plain assignment / `doc_ = Token{};` and make `Eat` not depend on moved-from
state.

---

## PARSE-008 — Double incref in `AdjustInlineExport` leaks the exported name
**Severity:** Medium (leak) · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:2604, 2612`

**Fix verified (by inspection).** Both branches of `AdjustInlineExport` now do
`this->exports.emplace_back(id->value)` with no manual `O_INCREF` — the
`Handle<T>` pointer constructor already increfs once, matching `ParseFunction`
(`exports.emplace_back(func->name)`). Each `pub x := …` no longer over-increfs
the name by one, so the `ORString` is released correctly.

<details><summary>Original report</summary>

```c
this->exports.emplace_back(O_INCREF(id->value));
```

`Handle<T>`'s pointer constructor already does `O_INCREF`
(`oobject.h:27`), so each `pub x := ...` bumps the name's refcount by 2 while
the handle releases only 1 → the `ORString` leaks. Compare `ParseFunction`
(`parser.cpp:2183`) which correctly does `exports.emplace_back(func->name)`
with no manual incref.

**Fix:** drop the `O_INCREF` in both branches of `AdjustInlineExport`.

</details>

---

## PARSE-007 — `pub` before a decorator is silently dropped
**Severity:** Medium · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:2059-2062` (`ParseStatement`)

**Fix verified (confirmed live; PoC `poc/parser/parse-007.orb`).** The `DECORATOR`
branch now rejects a modifier it cannot carry instead of dropping it:

```c
case TokenType::DECORATOR:
    if (access != AccessModifier::PRIVATE)
        throw ParserException(0);
    return this->ParseDecorator();
```

`pub @[deco] func f()` is now a diagnosed error ("…can only be applied to
declarations…") instead of silently declaring a private `f`. `@[deco] pub func f()`
still works.

<details><summary>Original report</summary>

`ParseStatement` consumes `pub`/`prot` into `access`, but the
`case TokenType::DECORATOR` branch calls `ParseDecorator()` which re-enters
`ParseStatement` with a fresh `access = PRIVATE`. `pub @[deco] func f()`
therefore declares a *private*, non-exported `f` with no diagnostic
(`@[deco] pub func f()` works).

**Fix:** pass `access` through to `ParseDecorator`, or reject modifiers before
decorators explicitly.

</details>

---

## PARSE-006 — SwitchCase checked through a `SwitchBlock*` cast (type confusion, UB)
**Severity:** Medium · **Status:** OPEN · **Location:** `parser.cpp:736` (`ParseSwitchStatement`)

```c
if (((SwitchBlock *) sw_case.get())->test == nullptr) {
```

`sw_case` is a `SwitchCase` (`vector tests; ASTNode *body; bool fallthrough`),
not a `SwitchBlock` (`ASTNode *test; vector cases`). The expression reads the
first pointer of `SwitchCase::tests` (the vector's begin pointer) and treats
"empty vector ⇒ null begin" as "default case". It works on current libc++/
libstdc++ by accident; it is UB and breaks with any vector layout change
(e.g. SBO).

**Fix:** `((SwitchCase *) sw_case.get())->tests.empty()`.

---

## PARSE-005 — Decorated methods are impossible in class/trait bodies **[confirmed]**
**Severity:** Medium · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:1200-1202` (`ParseBlock`)

**Fix verified (confirmed live; PoC `poc/parser/parse-005.orb`).** `DECORATOR` is now
in the class/trait body whitelist, so a decorated method parses and compiles.
`class B { @[foo] func m() {} }` compiles (was: error 71).

<details><summary>Original report</summary>

Same whitelist as PARSE-004: `DECORATOR` is missing, so any `@[deco]` inside a
class or trait body throws error 71/72, even though `ParseDecorator` happily
parses decorated functions elsewhere.

**PoC (reproduced):** `class B { @[foo] func m() {} }` → error 71.

**Fix:** add `TokenType::DECORATOR` to both class and trait whitelists (if
decorated methods are meant to be supported).

</details>

---

## PARSE-004 — `weak var` (without pub/prot) is unusable in class bodies **[confirmed]**
**Severity:** Medium · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:1200-1202` (`ParseBlock`)

**Fix verified (confirmed live; PoC `poc/parser/parse-004.orb`).** `KW_WEAK` is now in
the class-body statement whitelist, so `weak var` reaches the handler that
supports it (which also enforces CLASS context). `class A { weak var x = nil }`
compiles (was: error 71 "Invalid syntax within class definition").

<details><summary>Original report</summary>

The class-body statement whitelist (`CLEANUP, FUNC, INIT, LET, PUB, PROT,
VAR`) does not include `KW_WEAK`, so `weak var x = ...` throws error 71 before
`ParseStatement` (which *does* handle `weak`, and even requires CLASS context)
ever sees it. `pub weak var x` works because the line starts with `pub`.

**PoC (reproduced):** `class A { weak var x = 1 }` → "Invalid syntax within
class definition".

**Fix:** add `TokenType::KW_WEAK` to the whitelist.

</details>

---

## PARSE-003 — class/trait name is never validated **[confirmed]**
**Severity:** Medium · **Status:** FIXED (2026-08-31) · **Location:** `parser.cpp:141-142` (`ParseClassTrait`)

**Fix verified (confirmed live; PoC `poc/parser/parse-003.orb`).** `ParseClassTrait`
now guards the name token before reading its buffer:

```c
if (!this->Match(TokenType::IDENTIFIER))
    throw ParserException(16);
```

`class 123 { }` is now rejected (was: a class literally named `123` compiled).

<details><summary>Original report</summary>

After eating the `class`/`trait` keyword the parser uses
`tkcur_.buffer`/`length` directly as the name without checking
`Match(IDENTIFIER)`. Any token with a buffer becomes a "valid" name; tokens
without a buffer produce an empty name.

**PoC (reproduced):** `class 123 { }` compiles — a class literally named
`123` is declared.

**Fix:** `if (!this->Match(TokenType::IDENTIFIER)) throw ParserException(16);`
before reading the buffer.

</details>

---

## PARSE-002 — Empty doc/module comment crashes the compiler (SIGSEGV) **[confirmed]**
**Severity:** High · **Status:** FIXED (2026-07-27) · **Location:** `parser.cpp` (`GetDocString`) + `orstring.cpp`

An empty doc comment produces a token with `buffer == nullptr, length == 0`
(the scanner's `GetBuffer` on an empty StoreBuffer). `GetDocString` passed it
straight to `ORStringNewHoldBuffer`, which begins with
`assert(buffer[length] == '\0')` → null pointer dereference.

**PoC (was exit 139 / SIGSEGV):**
```
/*!*/
x := 1
```
`/** */` immediately before a `func`/`class` hit the same path via
`GetDocString(false)` (the comment body becomes empty after the scanner strips
leading whitespace).

**Fix:** an empty/null doc buffer is now treated as "no docstring". Verified:
the module-doc `/*!*/`, the empty and whitespace-only `/** */` before a
`func`/`class`, all compile and run cleanly, and non-empty docs are unaffected.

**PoC:** [`poc/parser/parse-002.orb`](poc/parser/parse-002.orb) `(fixed)` —
`# EXPECT: ok`, part of the poc gate.

---

## PARSE-001 — `continue` directly inside a loop is rejected
**Severity:** High · **Status:** FIXED (2026-06-15) · **Location:** `context.h` (`Context::CheckExt`)

**Fix verified (built + reproduced against `bin/Orbit`; PoC `poc/parser/parse-001.orb`).**
`Context::CheckExt` now starts the walk at `this` (`auto cursor = this;`) instead
of `this->back_`, so it includes the current context. A `continue` whose
immediate context *is* the `LOOP` now satisfies `CheckExt(LOOP)`.

Confirmed: `continue` directly in a loop compiles; `continue` in a nested loop
compiles; `continue` in a function with no enclosing loop and a top-level
`break` are still rejected with "Invalid break/continue…". (The `KW_CONTINUE`
case still falls through to the `KW_BREAK` check, which correctly keeps
`continue` from crossing a function boundary into an outer loop.)

> Side note found during verification: `continue` inside a `switch` inside a
> loop now *parses* fine but then trips a **codegen** assertion
> (`GetJBlockBegin`, `jblock.cpp:72`) — a separate issue to file when codegen is
> audited, not a parser defect.

<details><summary>Original report</summary>

```c
case TokenType::KW_CONTINUE:
    if (!this->context_->CheckExt(ContextType::LOOP))
        throw ParserException(66);
```

`Context::CheckExt` started walking from `back_`, i.e. it **skipped the current
context**. When `continue` appeared directly in a loop body, the current context
*was* the `LOOP` context, so `CheckExt(LOOP)` looked only at the parents, found
nothing, and threw. (Nested-loop `continue` worked, which is why it could hide.)

</details>
