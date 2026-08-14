# One source of truth: the 0.2.0 plan

Sema decides what a program means.  CodeGen lowers that decision.  Where
CodeGen works a semantic fact out for itself, there are two answers to one
question, and the compiler is correct only for as long as they agree.

An audit of `lib/CodeGen` found **35 places** where it does.  Four adversarial
review rounds had already been finding the consequences one at a time without
naming the cause; this document names it, and — more usefully — says which of
the 35 are the same bug and which only look like it.

## Why CodeGen has a second front end

It carries five flat tables and its own constant folder:

| state | re-answers | uses |
|---|---|---|
| `typeAliases` | which declaration a *type* name denotes | 22 |
| `consts`, `requiredConsts`, `shadowedConsts` | the *value* of a constant | 33 |
| `schemaDefs_` | which declaration a *schema* name denotes | 4 |
| `scopes` / `findVar` used as a name oracle | which declaration *any* name denotes | 55 |
| `tryEvalConstInt` / `evalConst` | constant folding | 14 |

**None of them has a scope chain.**  `typeAliases`, `consts` and
`requiredConsts` are saved and restored around each procedure — a workaround
that concedes the flatness rather than fixing it — and `schemaDefs_` is not
even in that list.

## The partition that matters

"Delete `typeAliases`" is the wrong statement of the problem.  A flat table
keyed by spelling gives the right answer whenever the name it is asked about
was **written where it is being resolved**.  It gives the wrong answer when
CodeGen takes a denoter written *somewhere else* and re-resolves it *here*.

```pascal
type t = array[1..10] of integer;
var g: ^t;
procedure inner;
type t = array[1..2] of integer;   { a different t }
begin new(g) end;                  { g's domain was written OUTSIDE }
```

`new(g)` re-reads `g`'s domain denoter — a node from the outer scope — in the
inner procedure's environment, and allocates two elements for a ten-element
array.  Plain ISO 7185, heap corruption, shipped in 0.1.5 and 0.1.6.

Contrast a structured value constructor `r[a: 1]` written inside `inner`: the
name `r` is written there, so the innermost declaration is the *correct*
answer.  The table is right, and no amount of routing it through Sema would
change the result.

So the rule is not "stop using names".  It is:

> **A denoter must be resolved in the scope it was written in.**  Any site that
> walks a foreign node must ask Sema.  A site resolving a locally-written name
> may keep the table.

### Class A — walks a foreign node (this is R1)

These take a node or denoter that belongs to another scope and re-resolve it
in the current environment.  Every confirmed memory-corruption finding is here.

| site | what it re-resolves |
|---|---|
| `CodegenTypes.cpp:203` | any `NamedTypeNode` reached from a foreign denoter |
| `CodegenTypes.cpp:309` | `recordLayouts` memoises on (node, schemaCtx) while the layout also depends on ambient `consts`/`typeAliases` |
| `CodegenExprs.cpp:1467` | `resolveRecordStructType` case 2: `ve->typeNode`, then by spelling |
| `CodegenExprs.cpp:1759` | re-folds a declaration's `Low`/`High` against use-site `consts` |
| `CodegenStmts.cpp:839` | `new(p)`: the pointer's recorded denoter through `denoterOf`'s spelling walk |
| `CodegenRuntime.cpp:420` | `getFileElemType`: the file variable's element denoter, by spelling — **covered by the `NamedTypeNode` rule**; no separate change needed |
| `CodegenProcs.cpp:101` | interface `var` denoters lowered in the body's environment |
| `CodegenProcs.cpp:906`, `:917` | a constant's `llvm::Value` **emitted in one function** and read from another |
| `CodegenProcs.cpp:869` | enum ordinals pushed into the flat `consts` map |
| `CodegenSchema.cpp:34`, `:40` | `schemaDefs_`, flat and never restored |
| `CodegenSchema.cpp:181`, `:190` | discriminant names and body denoter, by spelling |
| `CodegenSchema.cpp:184` | **the archetype**: a body's extent expressions re-emitted at the allocation site |
| `CodegenSchema.cpp:527`, `:528` | a body component named by a type, sized at the use site |
| `CodegenStmts.cpp:359`, `:1114` | subrange bounds and `with` field offsets, re-emitted from the declaration |

`CodegenProcs.cpp:917` deserves its own line: it stores an `llvm::Value`
produced inside one function into a flat map that a later function reads.  That
is not a scope bug, it is a cross-function SSA reference.

### Class B — resolves a locally-written name (the table is fine)

`denoterOf`'s `TypeName` lookup for a structured value constructor, and the
`typeAliases` consumers reached only from a denoter written at the use site.
These need no change for correctness of *identity*.  Some carry unrelated
defects — see class C.

### Class C — not about names at all

Grouping these with the rest is what made the problem look bigger and vaguer
than it is.  They belong to the later phases:

- **R2, folding and representation** — the 255 fallback for a capacity that did
  not fold (`CodegenTypes.cpp:397`); `maxchar` stored as `i8` `0xFF` and read
  back with `getSExtValue()`, so it folds to −1 (`CodegenTypes.cpp:74`,
  latent: every legal use produces a non-positive count that trips the existing
  fallback); case labels lowering to a *load* (`CodegenStmts.cpp:634`);
  `eof`/`eoln` checking two of the several things a name can denote
  (`CodegenExprs.cpp:47`).
- **R4, one layout engine** — `Sema::layoutVariantCase` is a hand-written
  mirror of `Codegen::Impl::layoutVariantCase`, and **only total size is ever
  compared; field offsets are compared by nobody** (`CodegenTypes.cpp:218`,
  `CodegenSchema.cpp:570`).
- **R5, resolve once** — an access path walked twice per statement, so a
  side-effecting subscript runs twice (`CodegenSchema.cpp:725`,
  `CodegenIO.cpp:286`).
- **R6, facts inferred from representation** — `cap = 1` for a comparison
  operand that is not a literal (`CodegenExprs.cpp:415`); component width taken
  from whatever `emitExpr` happened to produce (`CodegenIO.cpp:64`).

## Why the existing gates could not see any of this

Every one of these was found by reading, not by a failing test, and that is the
part worth fixing first.

- **1,738 tests, ASan+UBSan, IR byte-identity, `-O0`–`-O3`** were all green
  through ten schema defects and two shipped heap corruptions.  They measure
  the ISO 7185 core.
- **ASan instruments the compiler, never the program it emits.**
- **Every test's oracle is a printed value**, and a corrupted *neighbouring*
  field does not change it.  Both shipped corruptions printed correct output on
  the field the test looked at.

Two gates now exist because of that, and they belong before the fixes rather
than after:

- **`test/tools/guardheap.c`** — interposes `calloc` (which `plang_new` calls,
  and which resolves dynamically even though the runtime is linked statically)
  and butts every allocation against a `PROT_NONE` page, so an over-run faults
  at the write.  Demonstrated on `array[lo..hi] of packed record …`, which
  printed visible garbage from the fifth element and **exited 0**.
- **The `SchemaDifferential` harness** — one body compiled through every
  lowering (instance, `^t` + `new`, `v: t` parameter), with writing and reading
  **crossed** between forms.  The crossing is the whole trick: a layout that is
  wrong but self-consistent writes and reads the same wrong offsets and looks
  perfect.  Two earlier versions of this harness did not cross, and real
  defects walked through both.

## Order

Some class A sites need no edit of their own: they reach a foreign node
*through* `llvmTypeOfNode`, so the `NamedTypeNode` rule already fixes them.
`getFileElemType` is one — an array-typed file component was sized from an
inner procedure's homonym and corrupted the heap, and it is correct now without
`CodegenRuntime.cpp` being touched.  Each such site still earns a regression
test, because what covers it today is one condition in another file.

1. **R1 — class A.**  Route every foreign-node site through Sema's annotation.
   Start here because the size-agreement ICE already fails loudly wherever the
   two answers differ, so the tree stays honest during the change.
2. **R2** — one constant folder, in Sema.  Needs `ExprNode::ConstVal` and
   `IdentExpr::ResolvedSym`, neither of which exists today.
3. **R4** — one `LayoutEngine`, with field offsets compared and not just sizes.
4. **R3** — `ExtentForm`: a closed arithmetic form over discriminant *indices*
   with every other leaf pre-folded, so CodeGen never re-resolves an identifier
   in a schema body.  This deletes the archetype rather than guarding it.
5. **R5**, then **R6**.

R3 is what the undiscriminated-schema branch needs before it can merge; its
run-time layout walk is a third AST walk written because there was no single
layout to instantiate.
