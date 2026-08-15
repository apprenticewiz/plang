# Review 5: what is left, and what was learned trying

Review 5 found 27 unique defects; 24 survived adversarial verification and 20
are fixed.  This records the four still open, with what an attempt on each
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
