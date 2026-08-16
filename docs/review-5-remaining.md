# Review 5: what is left, and what was learned trying

Review 5 found 27 unique defects; 24 survived adversarial verification and **all
24 are fixed**.  This records what an attempt on the awkward ones actually cost,
so the next person does not rediscover it, and the three findings the
verification pass refuted so they are not re-filed.

An accounting note, since this file got it wrong for a while: it claimed one
defect was still open while every section below was marked FIXED or REFUTED.
The discrepancy was real and the file was the thing at fault -- the open defect
was `CodegenTypes.cpp:263`, which never got a section here because it never got
an attempt.  It is §6 now.  A running count kept by hand drifts; the sections
are the record.

## 1. A bare `string` as a VAR parameter — FIXED

Was: `procedure p(var s: string)` rejected every actual with
`expected 'string(255)', got 'string(10)'`.

Two earlier attempts were reverted, and the reason is the point.  Making it
COMPILE takes three small changes.  Making it WORK takes widening every string
operator at once, because the ones left narrow are the ones that then supply the
answers:

- the predicate `isVarStringLike` (Sema/Type.h) — but widening the predicate and
  not the capacity ACCESSOR beside it compared `v := 'hi'` against the
  instance's own StrCapacity of 0 and rejected it as not fitting a `string(0)`;
- assignment must take the string path rather than the whole-schema copy, or
  `s := 'zz'` is "assignment between schematic variables that codegen cannot
  locate";
- `exprStrCapV` must recover the capacity for a FORMAL and not only for `q^` —
  it travels in the same place, as the discriminant beside the pointer — or the
  formal carries the probe's `string(1)` and even `s := 'zz'` raises.

The capacity now travels with the actual, so one procedure body is bounded
differently per call: eight characters fit a `string(10)` and raise on a
`string(4)`.

## 2. A schema whose body is a discriminated schema — FIXED

Was: `type v2(n) = vec(n); var x: v2(4); x[1] := 7` rejected as a subscript of
a non-array type 'vec(4)'.

The cause was not the subscript check.  "Look through the schema to what it
really is" is a LOOP, because EP §6.4.7 lets a body be another instantiation,
and it was written as a STEP in a dozen places.  Each one then answered a
question about `vec(4)` where the answer had to be about `array[1..4]`, and the
consequences differed by site -- which is why they were found one at a time:

- Sema's subscript check refused the program outright;
- codegen's index path kept a lower bound of 0 and range-checked a 1..4 array
  as 0..3, so `x[4]` trapped and `x[0]` -- outside the array -- did not.

`schemaUnderlying()` in Sema/Type.h is now the one answer, and the peel sites
call it.  The lesson is the one this file already recorded twice: the widening
is wrong until every site has it, because the sites that are still narrow are
the ones supplying the extents.

## 3. ISO 10206 import-part with more than one specification — FIXED

Read from the standard rather than from the finding, and the finding's repro was
wrong.  ISO 10206 §6.11.3:

    import-part = [ `import' import-specification `;' { import-specification `;' } ] .
    import-specification = interface-identifier [ access-qualifier ] [ import-qualifier ] .

The keyword appears ONCE and introduces a list, each specification ended by its
own semicolon — `import A; B;`.  The separator is `;`, not the `,` the finding
used; `import A, B;` is not Pascal and is still a syntax error.  plang required
`import` before every specification, so the standard spelling did not parse and
only the repeated form did.

The list ends at the first token that is not an identifier, which is
unambiguous: every declaration-part that may follow, and the statement part,
begins with a reserved word.  The qualifiers belong to the specification and not
to the part, so `import A qualified; B only (X);` works per specification.

The repeated-keyword form is kept as plang's extension — it costs nothing and
programs have been written against it — but it is the extension, not the rule.

## 4. The probe's discriminant value of 1 in diagnostics — FIXED

A schema body is resolved once with its discriminants bound to 1, to get its
element and field TYPES.  Its extents are the probe's and are marked
`ExtentVaries` so nothing uses them — except the bound check, which diagnosed
them and rejected legal programs:

    type t(n: integer) = record a: array[2..n] of integer end;   { 2..1 }
    type t(n: integer) = array[1..n-1] of integer;               { 1..0 }

with a message quoting bounds the program never wrote.

`foldBounds` now suppresses the inverted-bound error when the probe is active
AND the bound actually READ a discriminant.  Narrow on purpose: `array[5..2]`
inside a schema body reads none, is empty in every instantiation, and is still
refused; and a bound that does read one is checked where it is real, since each
instantiation resolves the body again with its own values — `t(1)` for a body of
`array[2..n]` is refused there, quoting that instantiation's numbers.

## 6. A variant field laid out from a stale annotation — FIXED

R4 gave a record's FIXED fields the type Sema resolved for that record, and
stopped there.  A variant alternative's fields went on reading their own
denoter — and one declaration node serves every instantiation, carrying
whichever Sema resolved LAST.

    type inner(m: integer) = record a: array[1..m] of integer end;
         outer(n: integer) = record k: integer;
            case tag: boolean of true: (x: inner(n); y: integer);
                                 false: (z: integer) end;
    var big: outer(6); small: outer(2);

`outer(6)` was laid out with `outer(2)`'s offsets: writing `big.x.a[6]` landed
on `big.k`, and reading it back gave k's value.  Both instantiations have to be
in the program for it to show, and the one declared *last* is the one whose
layout the other gets.

`semaFieldType()` is now one lookup serving both parts of the record.  Sema's
`RecordFields` is flattened — §6.4.3.3 lets a variant field be selected by name
like any other, so every alternative's fields are in that list — which is what
makes one lookup enough.

## 5. `chr`/`succ`/`pred` unchecked — REFUTED, listed so it is not re-filed

The verification pass refuted this: the behaviour reproduces but is permitted,
and is a documented deliberate decision.  Same for the file-mode finding and for
the claim that the R4 offset gate has a blind spot over discriminated schema
records — that exclusion is deliberate and documented at both ends.

## The two process notes worth keeping

**A finding's repro may be wrong even when the finding is right.**  The
nested-instantiation defect (b0e7a2f) was reported against
`matrix(m,n) = array[1..m] of vector(n)`, which works and is already tested.  A
verifier found the real shape — a record-bodied nested instantiation reached by
field selection — while trying to refute it.

**Mutation-test every regression test.**  Three tests written during these fixes
passed against the parent commit and tested nothing: two `for..in` cases missing
the outer declaration that makes the flow state track the name, and a parser
case missing `kEP` when the branch under test is EP-only.  Reading them did not
show it; reverting `lib/` and re-running did.

---

# The sibling sweep

Nearly every defect fixed in reviews 5 and 6 was a **sibling**: a place where a
rule the codebase already has was not applied.  Not scattered bugs — one rule,
applied once.

    the array constructor arm checked its component values | the record arm beside it did not
    the fixed part rejected duplicate field names          | the variant part did not
    writtenInitialState followed Denotes                   | the descent 7 lines away used denoterOf
    the protected check walked index paths                 | not field paths, and only from assignment
    records and enums got declaration identity             | schemas kept comparing spellings
    R4 typed fixed fields from Sema                        | variant fields kept the stale annotation
    schemaUnderlying made the peel a loop                  | descendIntoInstantiation stayed a step

So "where else is this question asked?" is a step, not an afterthought.  A first
pass, by rule:

## 1. `denoterOf` callers — a spelling walk over a possibly-foreign node — SWEPT

    lib/CodeGen/CodegenExprs.cpp:1311, 1767, 1770
    lib/CodeGen/CodegenStmts.cpp:931

Each needs the question asked of it: is the node it walks written HERE, or
reached from another scope?  The second kind must follow
`NamedTypeNode::Denotes` instead, as `writtenInitialState` and
`initialStateShapeOf` now do.

- `CodegenExprs.cpp:1767,1770` (`emitStructuredValue`'s own `shape` lookup) —
  FIXED.  `denoter` is reached by recursing into a FOREIGN declaration exactly
  as `initialStateShapeOf`'s own comment describes — a record's
  `fd.Type.get()` (`fieldDenoter`), an array's `atn->Element.get()` — so an
  untyped nested component-value, `var r: rec value [f: [1:10; 2:20; 3:30]]`,
  took its shape from whatever the LOWERING PROCEDURE's own homonym `comp`
  happened to be, not from `rec`'s real field type.  A procedure with an
  unrelated local `type comp = record ...`  turned the array literal into
  `LLVM ERROR: plang codegen: array constructor has no array declaration...`
  — an abort, not a diagnostic.  Fixed by switching both calls from
  `denoterOf` to `initialStateShapeOf`, which already exists for this exact
  pattern and follows `NamedTypeNode::Denotes` (recorded by Sema in the scope
  the name was actually written in) instead of `typeAliases` (flat, keyed by
  spelling, rebuilt per procedure).  Test:
  `EPConstructor.AnUnnamedNestedComponentValueIsShapedByTheFieldsOwnDeclaration`.
- `CodegenExprs.cpp:1311` (`emitIndex`'s array-alias lower bound) — CHECKED,
  left alone.  `ntn` here is the indexed variable's OWN declared type node,
  not a foreign field/element recursion, and whatever wrong `Low` this branch
  computes is unconditionally overwritten a few lines down by the
  Sema-type-based answer (`T = schemaUnderlying(e.Array->ResolvedType.get())`)
  whenever `e.Array->ResolvedType` is set — which it is for every expression
  that reaches this point with a declaration to read.  Narrower than the
  emitStructuredValue case and not reproduced; left as a candidate rather than
  a confirmed fix.
- `CodegenStmts.cpp:931` (`new(p)`'s domain-denoter lookup, for `p`'s `value`
  clause) — CHECKED, left alone.  Already guarded by a size-agreement check
  the surrounding comment documents on purpose: the resolved denoter's size is
  compared against Sema's own answer, and a mismatch is treated as "the
  denoter was re-resolved somewhere else" and discarded.  A same-size
  collision could still slip through silently, but that is narrower than the
  emitStructuredValue case (which had no such check at all) and not
  reproduced.

## 2. One-level `SchemaBody` peels — should they loop? — SWEPT

    lib/CodeGen/CodegenSchema.cpp:260, 270      storage type, body size
    lib/CodeGen/CodegenExprs.cpp:1410
    lib/CodeGen/CodegenStmts.cpp:1273
    lib/CodeGen/CodegenProcs.cpp:372
    lib/Sema/SemaExpr.cpp:361, 1253

`schemaUnderlying()` exists for exactly this.  Each site needs deciding on
purpose: a body that is itself an instantiation is legal EP, so a single hop is
right only where the immediate body is what is wanted.

Every site above is now resolved:

- `CodegenSchema.cpp:260,270` (`schemaStorageType`, `schemaBodySize`) — FIXED.
  `q^` for `C(n) = B(n)`, `B(m) = string(m)` sized its `new(q, 20)` allocation
  from the probe's `string(1)` and every capacity check after it saw 1, not 20.
  Test: `EP7Schema.APointerToASchemaOfASchemaOfAStringReadsAndSizesAsAString`.
  (Sema's own one-hop for the same case — `checkDeref` returning the schema
  type instead of the string when the body is nested — is the reason the
  program reached codegen at all; fixed alongside, in `SemaExpr.cpp`, not at
  line 361.)
- `CodegenExprs.cpp:1410` (`recordTypeOf`) — FIXED, together with the
  confirmed defect below; see there.
- `CodegenStmts.cpp:1273` (`emitWith`'s static branch) — FIXED, and its Sema
  counterpart in `pushWithScope` (`SemaStmt.cpp`, two sites — undiscriminated
  and discriminated) needed the same widening or the names were never bound.
  `with x do id := 5` for a declared `x: B(6)` raised "undefined identifier
  'id'".  Test:
  `EP7Schema.WithOverASchemaOfASchemaBindsTheUnderlyingRecordsFields`.
- `CodegenProcs.cpp:372` — CHECKED, not a defect.  The one-hop `valTy` this
  computes is only ever consulted through `ve->type`, and when the body is a
  further schema instantiation, `llvmTypeOfSemaType`'s own `SchemaInstance`
  case already recurses — so `ve->type` lands on the real LLVM array type, and
  the generic `isa<ArrayType>(ve->type)` fallback in the index path unwraps it
  correctly regardless.  Confirmed with a two- and a three-level chain
  (`array of record`), both indexing correctly through a `var` schema
  parameter.  Left alone on purpose: the doc's own rule is that a single hop
  is right where the immediate body is what is wanted, and here something
  downstream already does the widening, so adding a second one would be the
  redundant kind, not the correcting kind.
- `SemaExpr.cpp:361` (deref, EP §6.4.7 VarString case) — FIXED.  Same
  `C(n) = B(n)` case as above at the Sema level: `q^` came back typed as the
  schema `C` rather than the string, so `writeln(q^)` was refused as
  unwritable.
- `SemaExpr.cpp:1253` (conformant-array actual matching) — FIXED.  `B(n) =
  A(n)` for an array `A` failed "conformant array parameter 'arr' requires an
  array argument, got 'B(5)'" — passing a nested-schema array where a
  directly-schema'd one already worked.  Test:
  `EP7Schema.ASchemaOfASchemaOfAnArrayConformsToAConformantArrayParam`.

Five real defects, all found by trying the sibling sweep's own question against
each listed site and constructing the two- or three-level nesting each site's
existing single hop couldn't reach.  All fixed, all mutation-tested against the
pre-fix code (each regression test confirmed to fail on the parent commit).

**One of these is already a confirmed defect**, found by this sweep and not by a
review — FIXED:

    type A(k: integer) = record a: array[1..k] of integer; id: integer end;
         B(n: integer) = A(n);
    procedure showB(var x: B); begin writeln(x.id) end;

    error: schema 'B' has no discriminant 'id'

A field of an undiscriminated schema formal is looked for one level down, so a
schema whose body is another schema has no fields at all.  `var x: A`, whose
body IS the record, works — the failure is the nesting.  Rejects-valid.

Both halves moved together, as §4 below said they had to: the Sema guard in
`checkField` (SemaExpr.cpp) now tests `schemaUnderlying(SchemaBody)->Kind`
instead of the immediate body's, and codegen's `recordTypeOf` and
`resolveRecordStructType` (CodegenExprs.cpp) call `schemaUnderlying` instead of
one hop.  Regression test:
`EP7Schema.AnUndiscriminatedSchemaWhoseBodyIsAnotherSchemaHasFields`
(test/Driver/ep_test.cpp) — mutation-tested against the pre-fix code, per the
lesson two sections up.

## 3. Questions asked of the KIND that belong to the STORAGE — SWEPT, one defect found

Two defects had this shape and both were memory corruption: the whole-value copy
branch asked for Array-or-Record and missed SchemaInstance, and `with` skipped
the access path for any SchemaInstance.  Any remaining `Kind == TypeKind::...`
test that decides *how something is laid out* is a candidate; the path knows and
the kind does not.

Swept every layout-deciding `Kind ==` check in `lib/CodeGen/*.cpp`.  Everything
that can plausibly reach a schema-instance component already calls
`schemaUnderlying()` (the prior sibling-sweep fixes, §2 above, widened exactly
these sites).  One looked like the exact bug shape --
`CodegenProcs.cpp:1091-1094`'s `storeInitialValue` `isAggregate` test
(`Array || Record`, missing `SchemaInstance`) -- and was tried against three
repros (an identifier and a field-access RHS for a `value`-initialized
schema-instance variable).  All three produced correct output, not corruption:
`emitExpr` for an aggregate-typed `IdentExpr`/`FieldExpr` always returns the
loaded VALUE (LLVM permits loading a whole struct/array into an SSA register),
never a pointer, so the existing no-op coercion plus a plain store already
copies correctly regardless of the missing `Kind` case.  The only RHS shape
that could reach that branch as a pointer -- a structured-value constructor --
is already refused for a schema-instance target at Sema
(`checkStructuredValue`, `err_constructor_not_aggregate`), confirmed by test;
and a function-call RHS can't reach a `value`-clause at all, since variables
(Phase 4) are lowered before procedures (Phase 5) register any symbols,
likewise confirmed.  Checked, not a defect.

The one candidate left speculative was pursued further and confirmed —
FIXED.  `CodegenExprs.cpp`'s per-dimension `Kind == TypeKind::Array` test in
conformant-array indexing, past the conformant dimensions and into the STATIC
element type, missed a schema instantiation there: `row = vec(3)` for
`vec(n) = array[1..n] of integer`.  `a[lo][2]` for a
`var a: array[lo..hi: integer] of row` fell to the untyped i64 GEP fallback —
no lower-bound subtraction AND an 8-byte stride instead of the array's own
24 — and landed the write one whole element past where it belongs (`m[1][3]`
got the value meant for `m[1][2]`), silently, exit 0.  Fixed by resolving the
element's Sema type through `schemaUnderlying` before the `Kind` test, the
same widening as every other site in §2.  Test:
`EP6ConformantArray.ASchemaInstanceElementTypeIndexesWithTheRightBounds`,
mutation-tested against the pre-fix code.

## 4. Checks that exist for one construct and not its neighbours — SWEPT, three defects found

`isVarStringLike` was needed by assignment, `+`, `length`, comparison AND substr
together.  The same question applies to any predicate added for one operator.

ISO §6.4.3.2's OTHER string shape, `packed array[1..n] of char`
(`isCharStringType`, `Type.h`), has the identical requirement and was missing it
in three of its siblings.  Assignment and comparison already widen for it
(`exprIsStringLike` includes `exprIsCharStr`); `read`/`write` do too.
`length`/`substr`/`trim`/`index` did not, asking only `exprIsVarStr` — FIXED:

- `CodegenExprs.cpp`'s `getStrArgPtr` (feeds all four builtins) and
  `strArgCapStatic` (feeds substr/trim's result sizing) now check
  `exprIsCharStr` and build the temporary VarString the same way the
  comparison operators already do (`emitCharStrAsStr`).  Before the fix,
  `length(a)` for a char-string `a` fell to a `strlen(ptr)` fallback that
  loaded the whole fixed-size array as an LLVM value where a pointer was
  wanted — an LLVM IR verifier abort, not a diagnostic — and
  `substr`/`trim`/`index` link-failed on a runtime symbol codegen never
  emitted a definition for.
- `SemaExpr.cpp`'s substr/trim return-type rule (`isVarStringLike(ArgTy) ?
  ArgTy : TyStr`) now also recognizes `isCharStringType`, returning a
  `string(n)` of the array's own length instead of falling through to the
  generic 255-capacity placeholder.
- `exprStrCapV` (`CodegenSchema.cpp`) now answers a char-string's own length
  instead of 0 — needed because substr/trim's capacity-forwarding case
  recurses into it on the argument, and 0 would have capped a chained
  `substr(substr(charArr, ...), ...)` at zero characters.

Test: `CharStringType.WorksWithLengthSubstrTrimAndIndexLikeAnEPString`
(test/Driver/codegen_test.cpp) — mutation-tested against the pre-fix code (the
LLVM IR verifier failure reproduces exactly).

One further gap was found and NOT fixed: `+` concatenation
(`SemaExpr.cpp`, the `Plus` case) also omits `isCharStringType`, so
`charArrA + charArrB` is rejected as non-numeric where the same two operands
already compare equal.  Left alone: `isCharStringType`'s own doc comment
enumerates exactly the powers ISO §6.4.3.2 gives the type — "written, compared,
and assigned a string literal" — and concatenation is not among them, so this
reads as a deliberate scope limit rather than a missed sibling.  Flagged here
in case a closer reading of §6.8.3.6 says otherwise.

### Why that one needs both halves at once — an attempt, reverted, then landed

The Sema guard tests the IMMEDIATE body's kind where it should test the
underlying one.  Correcting just that makes the program compile and then die in
codegen:

    LLVM ERROR: plang codegen: record has no field named 'id'

because the field LOOKUP peels one level too.  Sema accepting what codegen
aborts on is strictly worse than the clean rejection it replaces, so the attempt
was reverted — as two earlier attempts recorded above were, for the same reason.

Both halves have to move together:

    lib/Sema/SemaExpr.cpp   the `Undiscriminated && SchemaBody->Kind != Record`
                            guard — use schemaUnderlying(SchemaBody)
    lib/CodeGen             the field lookup resolving a name against the body's
                            RecordFields, which needs the same widening

That pairing is the shape of the remaining schema work, not a detail of this one
defect: Sema decides a program is legal and CodeGen must be able to lay it out,
and widening either alone turns a diagnostic into an internal error.

Both halves moved together on the next attempt — see item 2 above, where it is
recorded as FIXED.
