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

## 1. `denoterOf` callers — a spelling walk over a possibly-foreign node

    lib/CodeGen/CodegenExprs.cpp:1311, 1767, 1770
    lib/CodeGen/CodegenStmts.cpp:931

Each needs the question asked of it: is the node it walks written HERE, or
reached from another scope?  The second kind must follow
`NamedTypeNode::Denotes` instead, as `writtenInitialState` and
`initialStateShapeOf` now do.

## 2. One-level `SchemaBody` peels — should they loop?

    lib/CodeGen/CodegenSchema.cpp:260, 270      storage type, body size
    lib/CodeGen/CodegenExprs.cpp:1410
    lib/CodeGen/CodegenStmts.cpp:1273
    lib/CodeGen/CodegenProcs.cpp:372
    lib/Sema/SemaExpr.cpp:361, 1253

`schemaUnderlying()` exists for exactly this.  Each site needs deciding on
purpose: a body that is itself an instantiation is legal EP, so a single hop is
right only where the immediate body is what is wanted.

**One of these is already a confirmed defect**, found by this sweep and not by a
review:

    type A(k: integer) = record a: array[1..k] of integer; id: integer end;
         B(n: integer) = A(n);
    procedure showB(var x: B); begin writeln(x.id) end;

    error: schema 'B' has no discriminant 'id'

A field of an undiscriminated schema formal is looked for one level down, so a
schema whose body is another schema has no fields at all.  `var x: A`, whose
body IS the record, works — the failure is the nesting.  Rejects-valid.

## 3. Questions asked of the KIND that belong to the STORAGE

Two defects had this shape and both were memory corruption: the whole-value copy
branch asked for Array-or-Record and missed SchemaInstance, and `with` skipped
the access path for any SchemaInstance.  Any remaining `Kind == TypeKind::...`
test that decides *how something is laid out* is a candidate; the path knows and
the kind does not.

## 4. Checks that exist for one construct and not its neighbours

`isVarStringLike` was needed by assignment, `+`, `length`, comparison AND substr
together.  The same question applies to any predicate added for one operator.
