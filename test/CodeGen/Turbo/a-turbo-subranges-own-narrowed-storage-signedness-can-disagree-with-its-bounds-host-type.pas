(*
TypeContext::getSubrange's Turbo branch stamps a subrange's OWN Width/
IsSigned from TP7 ch.19's narrowestStorage rule, run over the subrange's
own written bounds -- not from its SubBase (the bounds' own host type,
usually plain Integer).  `0..40000` needs the sign bit for nothing (every
value is non-negative), but does not fit signed 16-bit either (40000 >
32767), so narrowestStorage picks (16, unsigned) -- Word-shaped -- even
though the literal bounds 0 and 40000 are themselves typed as plain signed
Integer.  ordinalIsUnsigned (OrdinalSignedness.h, formerly CodeGenImpl.h)
used to peel PAST a Subrange to read its SubBase's IsSigned instead of the
subrange's own -- equivalent back when a subrange's IsSigned was always
just copied from its host at construction, but wrong once Turbo's
narrowestStorage branch could pick a DIFFERENT one: it read Integer's
IsSigned=true for a subrange whose own storage is actually unsigned, so
every consumer of ordinalIsUnsigned/operandIsSigned/exprIsSigned --
including CGBinaryOps' own already-correct arithmetic, untouched by this
fix -- sign-extended a value like 40000 (0x9C40 as i16) into a large
NEGATIVE i64 instead of zero-extending it to the true 40000 (issue #177's
sibling audit, found auditing the pattern this issue's own three
confirmed sites share).  Fixed by reading a type's own IsSigned directly,
with no Subrange-to-SubBase peel.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:c=40000
CHECK-NEXT:gt
*)

program p;
type
  Big = 0..40000;
var
  b: Big;
  c: LongInt;
begin
  b := 40000;
  c := b + 0;
  writeln('c=', c);
  if b > 30000 then writeln('gt') else writeln('le')
end.
