# tools/ — developer tools

Small programs built on the Orbiter library to look inside the compiler. They
are built with the project (`ORBIT_BUILD_TOOLS`, on by default when Orbit is the
top-level project), land next to `Orbit` in the build's `bin/`, and are not
installed.

## orbit-astdump

Parses an Orbit source file and prints its AST and its scope tree. Nothing is
compiled to bytecode and nothing runs.

```sh
./build/debug/bin/orbit-astdump file.orb            # AST, then scopes
./build/debug/bin/orbit-astdump --ast file.orb      # AST only
./build/debug/bin/orbit-astdump --scopes file.orb   # scope tree only
```

Every AST node is printed on one line, indented by depth:

```
TRY                6:5-10:6 [44, 128)  «try { io.print(a) } catch e { if e.kind == @X { r...»
  try: BLOCK 6:9-8:6 [48, 75)  «{ io.print(a) }»
```

- `line:col-line:col [start, end)` is the node's location; the offsets are
  half-open;
- the text between `« »` is the source that location covers, with whitespace
  collapsed: a node whose text does not start and end where the construct
  does has a location bug;
- details follow the location: names, literal values, and for an identifier
  the symbol it resolved to (`-> kind/storage @declaration-offset`).

The scope tree lists every named scope (module, function, class, trait,
native function) with its nested scopes, their offset ranges and the symbols
declared in each. Two nested scopes of the same parent must not overlap, and
a scope must cover every scope nested in it.

A file that does not parse prints the parser's error with its line and
column, and exits with status 1.
