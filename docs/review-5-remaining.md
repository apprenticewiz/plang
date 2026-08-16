# Review 5: what is left, and what was learned trying

Review 5 found 27 unique defects; 24 survived adversarial verification and 23
are fixed.  This records the one still open, with what an attempt on each
actually cost, so the next person does not rediscover it.

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
