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
| `CodegenTypes.cpp:309` | `recordLayouts` memoises on (node, schemaCtx) while the layout also depends on ambient `consts`/`typeAliases` — **the poisoning source is fixed; the memo key itself is still wrong in principle** |
| `CodegenExprs.cpp:1467` | `resolveRecordStructType` case 2: `ve->typeNode`, then by spelling |
| `CodegenExprs.cpp:1759` | re-folds a declaration's `Low`/`High` against use-site `consts` — **covered by the array-bound rule** |
| `CodegenStmts.cpp:839` | `new(p)`: the pointer's recorded denoter through `denoterOf`'s spelling walk |
| `CodegenRuntime.cpp:420` | `getFileElemType`: the file variable's element denoter, by spelling — **covered by the `NamedTypeNode` rule**; no separate change needed |
| `CodegenProcs.cpp:101` | interface `var` denoters lowered in the body's environment — **covered by the array-bound rule** |
| `CodegenProcs.cpp:906`, `:917` | a constant's `llvm::Value` **emitted in one function** and read from another |
| `CodegenProcs.cpp:869` | enum ordinals pushed into the flat `consts` map — probed through records, inline records and file components; no reachable defect found |
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

- **R2, folding and representation — DONE.**  `ExprNode::ConstVal` carries the
  value Sema folded, in the scope the expression was written in, and both
  `tryEvalConstInt` and `constantValueOf` ask for it first.  The `255` capacity
  and `= 0` constant fabrications are gone; `IdentExpr::UserDeclared` replaced
  codegen's guessing at which of its tables held a spelling; a case label is
  now required to be a constant.

  **`ConstVal` holds ordinals only, and that is not a gap in R2.**  Sema folds
  ordinals and nothing else — it has `Symbol::ConstOrdinal` and no real
  equivalent — so a real or string constant has exactly ONE folder,
  `evalConst` in codegen, and no second answer to disagree with.  Making Sema
  fold reals is *building a folder*, not fixing a duplicated one, and it
  belongs to whoever wants constant reals diagnosed rather than to this work.

  Not a `const Symbol*` on `IdentExpr`, as first planned: symbols live in a
  `std::vector<Scope>` whose `popScope()` destroys them, so the pointer would
  dangle — the same defect as the raw `Type*` in `~TypeContext`.
- **R4, one layout engine** — `Sema::layoutVariantCase` is a hand-written
  mirror of `Codegen::Impl::layoutVariantCase`, and **only total size is ever
  compared; field offsets are compared by nobody** (`CodegenTypes.cpp:218`,
  `CodegenSchema.cpp:570`).
- **R5, resolve once — DONE.**  Worse than the audit said: with a counting
  function in the subscript, `q^.a[next].s` walked its path **three** times in
  a comparison, a write, a `length`, a whole-value assignment, a substring
  assignment and a read.  `emitAssign` had already been fixed for exactly this,
  which is *why* the others survived — the idiom `{emitStrAddr(x),
  exprStrCapV(x)}` was copied to every site needing a pair, so fixing the one
  somebody noticed left six.  They share `strAddrAndCap` now, and one test
  covers all six shapes because a test per site would repeat the mistake.
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

## Where the phases actually got to

**R1 and R2 are done** and their entries above are closed.

**R4 is partly done.**  The gate landed (Sema's field offsets checked against
codegen's on every record, fixed fields and variant fields alike), and one real
fix followed from it: the record arm of `llvmTypeOfSemaType` had the resolved
type in hand and passed only `RecordDecl` on, so the layout re-read each
field's denoter — and merely *declaring* an undiscriminated schema parameter
resized every discriminated instance of that schema.  **The offset gate was
green through that**, because both sides read the same stale annotation.  Two
answers agreeing is not the same as either being right, which is the argument
for gates that compare what the compiler DOES through different routes rather
than comparing two of its computations to each other.

**What R4 has since done**, and what it found:

- **A variant part's size and its field offsets are one walk.**  They were two
  functions, so every fact about the layout had to be stated twice — that a
  tagless selector reserves nothing, that only the outermost run is pre-aligned
  — and both of those were bugs that had to be fixed twice, because the copy
  that is not failing is not the copy anyone looks at.
- **The run-time walk is now gated against the static layout on every record
  the compiler lays out.**  On a record with nothing varying its arithmetic is
  all constants and folds without emitting an instruction, so the check is free
  and universal rather than sampled by whichever schema programs somebody
  thought to write.  This needed the walk to be *total* on fixed denoters.
- **It found a real under-alignment, present in every release.**  `set of char`
  is `i256` and wants 16-byte alignment; the blob a variant part reserves
  capped its cell at `i64`, so the run was 8-aligned while codegen emitted
  `align 16` accesses into it — a promise to LLVM that an aligned vector store
  may fault on.  It is in `iso7185pat.pas`, the standard's own acceptance test,
  in `vra`'s `cset` alternative.  The cap was written down THREE times:
  `Codegen::variantBlobType`, Sema's `variantBlobBytes`, and again as an
  explicit `min(BlobAlign, 8)` in `Sema::byteSizeOf`.  Fixing one made the
  other two disagree in turn, each caught by a different gate.
- **One function now answers what the shared run must be aligned to**, where
  the static layout used to accumulate it while the run-time walk computed it
  separately.  That took a distinction rather than a deletion: the outer tag
  bears on what the RECORD needs and not on what the run does, while a nested
  tag lives inside the run and does count.

Worth being exact about what "one engine" can mean here.  Within CodeGen the
two walks are converging on shared pieces — one variant traversal, one
run-alignment function — and the offset gate makes any remaining drift loud.
**Sema's walk is deliberately not merged into them.**  Sema answers sizes
without CodeGen by design, and an independent implementation that is checked on
every record is a differential oracle rather than a duplicate; the failure mode
this document warns about is two implementations that are *supposed* to be one
and drift silently, which is what the gate removes.  The alignment bug is the
evidence: the run-time walk had it right all along, and the only reason nobody
knew is that nothing asked it.

**R3 is done.**  Every extent in a schema body — array bounds, string
capacities, subrange bounds, the actuals of a nested instantiation, and the
bounds handed to a conformant array parameter — is a closed form over the
discriminants by *index*, with every other leaf folded in the scope the
declaration was written in.  No fallback re-emits a declaration's expression at
a use site; each was **measured at 0 tests and then deleted**, so an extent
arriving without a form is an internal error rather than a quiet re-resolution.
`bindSchemaDiscs`, which existed only to define the discriminant names so those
expressions could be re-emitted, is gone.  What replaced it, `RtDiscScope`, only
says which object's discriminants a form is evaluated against — and restores the
previous set, which the old pair did not: one call site had grown a hand-written
save/restore and the others had not.

Three things are worth carrying forward from how it went.

**The measurement found what reading had not.**  Replacing each fallback with an
internal error and running the suite gave 0 for capacities and 45 for array
bounds.  This document's earlier explanation of the 45 — nested instantiation
resolved under the outer probe — was wrong.  The form-recording block sat below
the `ArrayTypeNode` branch of `resolveTypeImpl`, and that branch returns, so
array bounds had never had forms at all.

**Reaching for the nearest available value is the same bug one level down.**
The first attempt at the capacity site preferred the form via `extentOf`, which
evaluates against the *ambient* discriminants, and failed 18 tests: a path's
capacity has to be evaluated against the discriminants of that path's own root.
Nearest-binding-of-a-name and nearest-set-of-discriminants are one mistake.

**Two of the last sites converted were live defects**, not tidying.  A subrange
`1..n*lim` was checked against a `var lim` in the assigning procedure, so a
legal assignment trapped; a conformant array parameter was told `1..6` for a
thirty-element actual because the bound was re-emitted in the caller's scope.
Both are the 0.1.5 heap corruption in a different lowering — three lowerings of
one defect, found by pulling on one thread rather than by three reviews.

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
2. **R2 — done.**  See the class C entry above for what landed and for the two
   places the plan turned out to be wrong: `ConstVal` is ordinal-only on
   purpose, and the identifier annotation is a flag rather than a `Symbol*`.
3. **R4** — one `LayoutEngine`, with field offsets compared and not just sizes.
4. **R3 — done.**  `ExtentForm` is a closed arithmetic form over discriminant
   *indices* with every other leaf pre-folded, and CodeGen no longer re-resolves
   an identifier in a schema body anywhere.  The archetype is deleted rather
   than guarded.
5. **R5**, then **R6**.

R3 is what the undiscriminated-schema branch needs before it can merge; its
run-time layout walk is a third AST walk written because there was no single
layout to instantiate.
