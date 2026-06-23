# Orbit Documentation Guide

How to document Orbit code. It covers two surfaces that share one tag
vocabulary:

1. **`.orb` source** — doc comments in Orbit code (`/** … */` and `/*! … */`).
2. **Native (C++) runtime methods** — docstrings passed to the
   `RUNTIME_FUNCTION` / `RUNTIME_METHOD` macros.

All documentation **must be written in English**.

---

## Where documentation lives

### `.orb` source

Orbit has two dedicated documentation-comment forms (ordinary `/* … */` block
comments and `#` line comments are **not** documentation — they are discarded):

- **`/** … */` — declaration doc.** Attaches to the declaration that
  immediately follows it: `func`, `class`, `trait`, and `native` declarations,
  including when prefixed with `pub`/`prot` or a `@[decorator]`. Blank lines
  between the comment and the declaration are fine.

  ```orb
  /**
  @brief Return the greatest common divisor of two integers.

  Uses the Euclidean algorithm; the result is always non-negative.

  @param a   First integer.
  @param b   Second integer.

  @return The GCD of `a` and `b` (0 when both are 0).

  @example
      gcd(12, 8)   // 4
  */
  pub func gcd(a, b) {
      # ...
  }
  ```

- **`/*! … */` — module doc.** Place it at the **top of the file**; it
  documents the module as a whole. Use it for the module's purpose, the public
  surface it exposes, and any usage notes.

  ```orb
  /*! Math helpers: small, allocation-free numeric utilities. */

  import "io"

  /** @brief ... */
  pub func gcd(a, b) { # ... }
  ```

### Native (C++) runtime methods

Documentation strings are passed inline to `RUNTIME_FUNCTION` and
`RUNTIME_METHOD` (see the complete examples near the end). They use the exact
same tags described below.

---

## Full Template

```
@brief One-line summary ending with a period.

Extended description. Explain what the method does, any non-obvious behavior,
whether the result is a new object or in-place mutation, how empty/zero/negative
inputs are handled, Unicode awareness, etc.
Omit this paragraph entirely when @brief is already self-explanatory.

@param name          Description of a required parameter.
@param name?         Description of an optional parameter (defaults to nil when omitted).
@param name=value    Description of an optional parameter with an explicit default.
@param ...name       Description of a variadic parameter (collects remaining positional args).
@param *name         Description of a keyword parameter.

@return Description of the return value. Omit this tag for methods that return Nil.

@panic ErrorType     Specific condition that triggers this panic.
@panic OOMError      When memory allocation fails.

@see related_method, other_method

@example
    receiver.method(arg)        // expected result
    receiver.method(edge_case)  // edge-case result
```

---

## Tag Reference

### `@brief`
- **Required.** Always the first line.
- One sentence, ends with a period.
- No Markdown formatting inside the brief line.

### Body paragraph
- **Optional.** Skip if `@brief` is fully self-explanatory.
- Separate from `@brief` by a blank line.
- Cover: mutation vs. copy semantics, Unicode handling, behaviour on empty
  input, negative indices, type coercion, thread safety if relevant.

### `@param`

| Form | When to use |
|------|-------------|
| `@param name` | Required positional parameter. |
| `@param name?` | Optional positional parameter; caller may omit it (defaults to nil). |
| `@param name=value` | Optional positional parameter with an explicit compile-time default. |
| `@param ...name` | Variadic: collects all remaining positional arguments into a List. |
| `@param *name` | Keyword argument map passed by the caller. |

- List parameters in call order.
- The implicit `self` receiver of a method is **never** documented as `@param`.
- The type is enforced at runtime (by `PCHECK` in native code), not repeated in
  the doc — describe the semantic role, not the type.

### `@return`
- Omit entirely for methods whose return type is `Nil`.
- One line describing what is returned and under what condition (e.g.
  `"The index of the first occurrence, or -1 if not found."`).

### `@panic`
- One tag per distinct panic condition.
- Format: `@panic ErrorType  Condition as a plain sentence.`
- **Always include** `@panic OOMError` when the method allocates heap objects,
  unless the method is trivially allocation-free.
- Common types: `OOMError`, `TypeError`, `ValueError`, `AttributeError`,
  `IndexError`, `NameError`.

### `@see`
- **Optional.** Comma-separated list of related method names.
- Use when there is a natural counterpart (`find` / `find_last`,
  `split` / `join`, `pop` / `append`, etc.).

### `@example`
- **Recommended** for any method whose behaviour is not immediately obvious
  from its name and parameters alone. Omit only for trivial one-liner methods
  (e.g. `is_empty`, `is_even`).
- Use a 4-space indent inside the block.
- Show the happy path first, then at least one edge case (empty input, zero,
  negative index, missing optional arg, etc.).
- Comments after `//` show the expected result.

---

## Panic conventions

| Panic | When to emit |
|-------|--------------|
| `OOMError` | Any method that allocates a new String, List, Dict, Tuple, etc. |
| `TypeError` | A required parameter has the wrong type. |
| `ValueError` | A parameter value is out of valid range (e.g. negative width, invalid base). |
| `IndexError` | An index is out of bounds (List, Tuple, Bytes). |
| `AttributeError` | Property or method not found on the target object. |
| `NameError` | A name used in LDGBL is not defined in the current context. |

---

## Complete Examples (native C++)

### Instance method — no parameters

```cpp
RUNTIME_METHOD(str_is_empty, is_empty,
    "@brief Return true if the string contains no characters.",
    0, false, false)
```

### Instance method — required + optional-with-default parameters

```cpp
RUNTIME_METHOD(str_pad_start, pad_start,
    R"DOC(
@brief Return a copy of the string left-padded to the given total width.

If the string is already at least `width` characters long, it is returned
unchanged.  Padding is applied in terms of Unicode codepoints, not bytes.

@param width        Total width of the resulting string (in codepoints).
@param ch=" "       Single character used for padding.

@return A new padded string.

@panic OOMError     When memory allocation fails.
@panic ValueError   When `width` is negative or `ch` is not exactly one codepoint.

@see pad_end

@example
    "hi".pad_start(5)       // "   hi"
    "hi".pad_start(5, "0")  // "000hi"
    "hello".pad_start(3)    // "hello"  (already wider than width)
)DOC",
    2, false, false)
```

### Instance method — variadic parameters

```cpp
RUNTIME_METHOD(list_concat, concat,
    R"DOC(
@brief Return a new list containing all elements of this list followed by those of `other`.

@param other    The list to append.

@return A new List containing the combined elements.

@panic OOMError  When memory allocation fails.
@panic TypeError When `other` is not a List.

@see append, prepend
)DOC",
    1, false, false)
```

### Static function — keyword argument

```cpp
RUNTIME_FUNCTION(error_create, create,
    R"DOC(
@brief Create a new error object with the specified kind, message, and optional details.

@param kind        Atom that categorises the error type.
@param reason      Human-readable description of what went wrong.
@param details?    Additional context attached to the error (defaults to nil).

@return A new Error object.

@panic OOMError   When memory allocation fails.
@panic ValueError When nil is passed to a non-optional parameter.
@panic TypeError  When a parameter has an invalid type.

@example
    Error.create(:io_error, "file not found")
    Error.create(:net_error, "connection refused", { url: "..." })
)DOC",
    3, false, false)
```

---

## Complete Example (`.orb`)

```orb
/*! Small string helpers layered on top of the String builtin. */

/**
@brief Return a copy of `s` with surrounding ASCII whitespace removed.

Only strips spaces, tabs, and newlines; interior whitespace is preserved.

@param s   The string to trim.

@return A new, trimmed string (the original when nothing was stripped).

@see trim_start, trim_end

@example
    trim("  hi ")   // "hi"
    trim("hi")      // "hi"
*/
pub func trim(s) {
    # ...
}
```

---

## Quick checklist before committing a doc string

- [ ] Written entirely in English.
- [ ] In `.orb`, used `/** */` for a declaration (or `/*! */` at the top of the
      file for the module) — not a plain `/* */` or `#` comment.
- [ ] `@brief` is a single sentence ending with a period.
- [ ] `self` is not listed as `@param`.
- [ ] Optional params use `?` or `=value` suffix.
- [ ] Variadic params use `...` prefix.
- [ ] Keyword params use `*` prefix.
- [ ] `@return` is omitted for Nil-returning methods.
- [ ] `@panic OOMError` is present when the method allocates.
- [ ] `@example` covers at least one edge case when the method is non-trivial.
- [ ] `@see` lists natural counterparts when they exist.
