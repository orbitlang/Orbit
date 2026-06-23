# parser — bug report

**Component:** `orbit/liftoff/parser` (parser.cpp, parser.h, context.h, ast.h) · **ID prefix:** `PARSE`
**PoCs:** [`poc/parser/`](poc/parser/) · **Last reviewed:** 2026-06-15
**Status:** OPEN · PARTIAL · FIXED · WONTFIX — see [GUIDE.md](GUIDE.md).

> Findings marked **[confirmed]** were reproduced against the current
> `bin/Orbit` binary.

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

---

## PARSE-002 — Empty doc/module comment crashes the compiler (SIGSEGV) **[confirmed]**
**Severity:** High · **Status:** OPEN · **Location:** `parser.cpp:2242` (`GetDocString`) + `orstring.cpp:1545`

An empty doc comment produces a token with `buffer == nullptr, length == 0`
(the scanner's `GetBuffer` on an empty StoreBuffer). `GetDocString` passes it
straight to `ORStringNewHoldBuffer`, which begins with
`assert(buffer[length] == '\0')` → null pointer dereference.

**PoC (reproduced, exit code 139 / SIGSEGV):**
```
/*!*/
x := 1
```
`/** */` immediately before a `func`/`class` hits the same path via
`GetDocString(false)` (the comment body becomes empty after the scanner strips
leading whitespace).

**Fix:** in `GetDocString`, treat a null/empty buffer as "no docstring"
(return `{}`); defensively, `ORStringNewHoldBuffer` should also handle
`buffer == nullptr`.

---

## PARSE-003 — class/trait name is never validated **[confirmed]**
**Severity:** Medium · **Status:** OPEN · **Location:** `parser.cpp:138-142` (`ParseClassTrait`)

After eating the `class`/`trait` keyword the parser uses
`tkcur_.buffer`/`length` directly as the name without checking
`Match(IDENTIFIER)`. Any token with a buffer becomes a "valid" name; tokens
without a buffer produce an empty name.

**PoC (reproduced):** `class 123 { }` compiles — a class literally named
`123` is declared.

**Fix:** `if (!this->Match(TokenType::IDENTIFIER)) throw ParserException(16);`
before reading the buffer.

---

## PARSE-004 — `weak var` (without pub/prot) is unusable in class bodies **[confirmed]**
**Severity:** Medium · **Status:** OPEN · **Location:** `parser.cpp:1088-1091` (`ParseBlock`)

The class-body statement whitelist (`CLEANUP, FUNC, INIT, LET, PUB, PROT,
VAR`) does not include `KW_WEAK`, so `weak var x = ...` throws error 71 before
`ParseStatement` (which *does* handle `weak`, and even requires CLASS context)
ever sees it. `pub weak var x` works because the line starts with `pub`.

**PoC (reproduced):** `class A { weak var x = 1 }` → "Invalid syntax within
class definition".

**Fix:** add `TokenType::KW_WEAK` to the whitelist.

---

## PARSE-005 — Decorated methods are impossible in class/trait bodies **[confirmed]**
**Severity:** Medium · **Status:** OPEN · **Location:** `parser.cpp:1088-1094` (`ParseBlock`)

Same whitelist as PARSE-004: `DECORATOR` is missing, so any `@[deco]` inside a
class or trait body throws error 71/72, even though `ParseDecorator` happily
parses decorated functions elsewhere.

**PoC (reproduced):** `class B { @[foo] func m() {} }` → error 71.

**Fix:** add `TokenType::DECORATOR` to both class and trait whitelists (if
decorated methods are meant to be supported).

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

## PARSE-007 — `pub` before a decorator is silently dropped
**Severity:** Medium · **Status:** OPEN · **Location:** `parser.cpp:1899-1900` (`ParseStatement`)

`ParseStatement` consumes `pub`/`prot` into `access`, but the
`case TokenType::DECORATOR` branch calls `ParseDecorator()` which re-enters
`ParseStatement` with a fresh `access = PRIVATE`. `pub @[deco] func f()`
therefore declares a *private*, non-exported `f` with no diagnostic
(`@[deco] pub func f()` works).

**Fix:** pass `access` through to `ParseDecorator`, or reject modifiers before
decorators explicitly.

---

## PARSE-008 — Double incref in `AdjustInlineExport` leaks the exported name
**Severity:** Medium (leak) · **Status:** OPEN · **Location:** `parser.cpp:2416, 2424`

```c
this->exports.emplace_back(O_INCREF(id->value));
```

`Handle<T>`'s pointer constructor already does `O_INCREF`
(`oobject.h:27`), so each `pub x := ...` bumps the name's refcount by 2 while
the handle releases only 1 → the `ORString` leaks. Compare `ParseFunction`
(`parser.cpp:2183`) which correctly does `exports.emplace_back(func->name)`
with no manual incref.

**Fix:** drop the `O_INCREF` in both branches of `AdjustInlineExport`.

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

## PARSE-010 — Walrus shadowing differs between single and tuple form
**Severity:** Medium (semantics) · **Status:** OPEN · **Location:** `parser.cpp:2044-2083` (`ParseWalrus`)

Tuple form declares the LHS symbols **before** parsing the RHS; single form
declares the LHS **after**. So in `x := x` the RHS resolves to the outer `x`,
while in `x, y := x, 1` the RHS `x` resolves to the *freshly declared* (still
uninitialized) `x`. One of the two is wrong, presumably the tuple form.

**Fix:** declare tuple symbols after `decl->value` is parsed, like the single
case.

---

## PARSE-011 — `ParsePrefix`: self-assignment instead of propagating loc
**Severity:** Low · **Status:** OPEN · **Location:** `parser.cpp:1831`

```c
prefix->value->loc.end = prefix->value->loc.end;   // no-op
```
Intended: `prefix->loc.end = prefix->value->loc.end;`. Every unary expression
(`-x`, `!x`, `~x`, `<-ch`…) keeps `loc.end` at the operator token —
wrong ranges in diagnostics.

---

## PARSE-012 — Char literals decoded via broken `StringUTF8ToInt`
**Severity:** Low · **Status:** OPEN · **Location:** `parser.cpp:1674` (`ParseLiteral`, NUMBER_CHR)

`UIntNew(isolate_, StringUTF8ToInt(tkcur_.buffer))`:
- For a byte ≥ 0xF1 (e.g. `'\xFF'`) `StringUTF8ToInt` returns **-1**
  (see UTF8-002), which silently becomes a huge unsigned value.
- A buffer holding a scanner-preserved invalid escape (e.g. `'\q'` → bytes
  `\`,`q`, see SCAN-011) is silently truncated to the first byte: the char
  literal evaluates to `0x5C` (`'\'`).

**Fix:** validate the buffer is exactly one well-formed codepoint and reject
otherwise; fix UTF8-002 first.

---

## PARSE-013 — `Parse()` is `noexcept` but std containers can throw
**Severity:** Low · **Status:** OPEN · **Location:** `parser.cpp:2544`

The catch clauses cover only project exceptions. Any `std::bad_alloc` from the
many `std::vector::push_back`s (or `std::length_error`) escapes a `noexcept`
function → `std::terminate` instead of a NOMEM `ParserError`.

**Fix:** add `catch (std::bad_alloc &)` → NOMEM, and a defensive
`catch (...)` → GENERIC_ERROR.

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

## PARSE-015 — Inconsistent OOM checks on `ORStringNew(...).release()`
**Severity:** Low (OOM paths) · **Status:** OPEN · **Location:** `parser.cpp:421, 503, 548, 564, 578, 651`

Some `ORStringNew` results are null-checked (`ct->name`, `imp_name->name`,
`bc->label`…), these are not: `imp->path` (IMPORT_FROM), `native_name` ×2,
`np->name`, `mod_name` ×2. On allocation failure the AST silently carries a
null field into later phases instead of throwing `DatatypeException`.

---

## PARSE-016 — `isalnum`/`isalpha` on signed `char` (UB for non-ASCII paths)
**Severity:** Low · **Status:** OPEN · **Location:** `parser.cpp:2436-2439` (`CheckSetImportAlias`)

The auto-alias scan feeds raw `char` values from the module path into
`isalnum`/`isalpha`. For UTF-8 path bytes ≥ 0x80, `char` is negative on
macOS/arm64 and passing negative values (other than EOF) to ctype functions is
UB. Cast to `unsigned char` first.

---

## PARSE-017 — `!func->constant` check is always true when self is added
**Severity:** Low (suspicious logic) · **Status:** OPEN · **Location:** `parser.cpp:2129-2136` (`ParseFunction`)

The `self` parameter is added when `!func->constant && CheckBack(CLASS|TRAIT)`
— but `func->constant` is only set *later* (the `const` qualifier is parsed
after the parameter list), so the condition is always true at this point.
Either `const` methods are supposed to skip `self` (bug: they don't) or the
check is dead and should be removed.

---

## PARSE-018 — Wrong error index for invalid label target
**Severity:** Low (diagnostics) · **Status:** OPEN · **Location:** `parser.cpp:1973-1974`

`if (stmt->node_type != NodeType::IDENTIFIER) throw ParserException(33);` —
index 33 is *"Invalid catch clause: expected @atom after catch"*. A statement
like `a + b: loop {…}` reports a catch-clause error. Should be a dedicated
message (34/35 family).

---

## PARSE-019 — Location/scope offsets use the token *after* the construct
**Severity:** Info (diagnostics/scopes) · **Status:** OPEN · **Location:** `parser.cpp:1107-1113, 708-712, 278-279`

- `ParseBlock`: `block->loc.end = TKCUR_START` and
  `LeaveNestedScope(TKCUR_END.offset)` run *after* `MatchEat(RIGHT_BRACES)`,
  i.e. they reference the token following `}` — block ranges/scope boundaries
  extend one token too far.
- `ParseSwitchCase`: `block->loc.end` is never updated, so case loc.end points
  at the colon area.
- `ParseForInStatement` passes `constexpr Position end{}` as the var-decl
  start → `{0,0,0}` start location.

---

## PARSE-020 — Meaningless access modifiers silently accepted
**Severity:** Info · **Status:** OPEN · **Location:** `parser.cpp:1844-1866` (`ParseStatement`)

`pub if x {}`, `pub import "io"`, `pub return`, `weak func …`, `pub pub var x`
— modifiers are parsed and then ignored by statement branches that don't use
`access`/`weak`, with no diagnostic. Reject modifiers when the following
statement can't carry them.

---

## PARSE-021 — `f(x=)` and `func f(a=)`: empty default/named value accepted
**Severity:** Info · **Status:** OPEN · **Location:** `parser.cpp:1451-1455, 2209-2212`

Both named-argument and named-parameter parsing make the value optional
(`if (!Match(COMMA, RIGHT_ROUND)) value = …`), so a dangling `=` produces a
named arg/param with `value == nullptr`. If "named without value" isn't a
deliberate feature, this should be a syntax error.

---

## PARSE-022 — Synthetic `init` skips the super.init requirement
**Severity:** Info (verify against codegen) · **Status:** OPEN · **Location:** `parser.cpp:2469-2491` (`ClassCheck`)

For derived classes with an explicit `init`, the first statement must be
`super.init(...)` (error 80). If the class has *no* init, `InjectInit`
synthesizes an empty constructor that does **not** call `super.init` — and no
error is raised. Either the synthetic ctor should inject the super call, or
codegen must handle implicit super-init (worth verifying).

---

## PARSE-023 — Anonymous function names collide across contexts
**Severity:** Info · **Status:** OPEN · **Location:** `parser.cpp:2398-2404` + `context.h:28`

`MakeFuncName` uses `context_->anon_count++`, but `anon_count` lives on each
`Context` and every function/class pushes a fresh one — two anonymous
functions in different enclosing functions are both named `func$0`. Fine while
names are scope-local; a problem if codegen ever assumes global uniqueness
(e.g. symbol dumps, debug info).

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
