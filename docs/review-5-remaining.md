# Review 5: what is left, and what was learned trying

Review 5 found 27 unique defects; 24 survived adversarial verification and 19
are fixed.  This records the five still open, with what an attempt on each
actually cost, so the next person does not rediscover it.

## 1. A bare `string` as a VAR parameter — attempted and BACKED OUT

`procedure p(var s: string)` rejects every actual:
`expected 'string(255)', got 'string(10)'`.

EP §6.7.3.1 admits a bare `string` as a parameter form, and §6.4.3.3 makes
`string` a schema whose one discriminant is the capacity.  As a VALUE parameter
this already works — the actual is copied into the widest capacity plang has,
and `UndiscriminatedString.*` covers it.  A VAR parameter cannot be copied:
ISO §6.6.3.3 requires the actual to be of the parameter's own type, so a formal
of one fixed capacity matches nothing.

**A three-line Sema fix makes it compile and gives the wrong impression.**
Routing a var-parameter's bare `string` to `stringSchemaType()`, accepting a
`VarString` actual against a string-schema formal, and teaching `schemaActual`
to pass the capacity as the discriminant is enough to make

    procedure show(var s: string); begin writeln('[', s, ']') end;

work.  It is not enough for the feature.  Inside the procedure the formal's type
is `Schema`, not `VarString`, so:

- `s := 'zz'` reaches `codegenICE("assignment between schematic variables that
  codegen cannot locate")`
- `s + '!'` is `error: operator '+' requires numeric operands, got 'string' and
  'char'`

which are the two things a var parameter exists to do.  That state is WORSE than
the rejection: a clean diagnostic became an internal error.  The attempt was
reverted for that reason, not because it was hard.

What it needs is for every string operation to treat a schema whose body is a
`VarString` as a string — the same widening `varStrTypeOf` did in CodeGen
(commit b187337), applied on the SEMA side to assignment compatibility, `+`,
`length`, comparison and `substr`.  That is the real shape of this work.

## 2. A schema whose body is a discriminated schema cannot be subscripted
   — attempted and BACKED OUT

    type vec(n: integer) = array[1..n] of integer;
         v2(n: integer)  = vec(n);
    var x: v2(4);  x[1] := 7;

`error: subscript operator applied to non-array type 'vec(4)'`.  The subscript
check in SemaExpr looks through a schema body ONCE, and one hop from v2(4)
lands on vec(4) -- still a SchemaInstance.

**Looping instead of hopping once makes it compile and gets the bounds wrong.**
The element type comes out right and `x[1] := 7` runs, but the index range
becomes `0..3` where the declaration says `1..4`:

    plang runtime: array index 4 out of bounds 0..3

so `x[4]` traps on a legal program and `x[0]` -- outside the array -- would be
accepted.  That is a memory-safety regression bought with a clean rejection, and
strictly worse than the diagnostic it replaces, so it was reverted.

The lower bound is being lost somewhere between the inner instantiation and the
index check; the count survives and the origin does not.  Whoever picks this up
should start by finding where `1..n` becomes `0..count-1` for a body reached
through a second schema, and should keep the range check ON while testing --
with `-fno-range-checks` this defect is silent.

Same family as 1: "look through the schema to what it really is" is not one
change but a property every operator has to have.

## 3. ISO 10206 import-part with more than one specification

`import a, b;` is rejected; only `import a; import b;` parses.  Unverified by me
beyond the finder's report and the verification pass.

## 4. The probe's discriminant value of 1 reaches user diagnostics

A schema body is resolved once with its discriminants bound to 1, and that 1
escapes into messages, rejecting legal programs whose body is only degenerate at
1.  R3 removed the probe from the extents that CodeGen uses; it is still what
Sema DIAGNOSES against.

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
