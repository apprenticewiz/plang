(*
CGBinaryOps' TokenKind::In arm was the one call site issue #177's own
sibling audit deliberately skipped in this file (every other arm there was
already correct) -- but it turned out not to be: it passed
`EmitExpr(*e.Left)` straight into SetOps::emitSetMember with no widening
and no signedness at all, instead of going through ToI64/operandIsSigned
the way every other arm here does.  emitSetMember's own setBitIndex already
had an `ordinalSigned` parameter (added for the set-typed side of this
exact class of bug) that the `in`-operator's own call simply never passed.
For a `ShortInt` (or any other narrow SIGNED Turbo ordinal) holding a
negative value, setBitIndex's default guess-from-LLVM-width fallback
zero-extended it, landing on the wrong bit and producing a wrong membership
answer -- e.g. -3 tested against `[-5..5]` read back as "not in" (issue
#519).  Fixed by passing operandIsSigned(*e.Left) through to
emitSetMember, matching every other arm in this file.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:neg3.in.fullset=TRUE
CHECK-NEXT:pos3.in.fullset=TRUE
CHECK-NEXT:zero.in.gapset=FALSE
CHECK-NEXT:neg3.in.gapset=TRUE
CHECK-NEXT:byte200.in.bset=TRUE
CHECK-NEXT:byte50.in.bset=FALSE
*)

program p;
type TRange = -5..5;
var s: ShortInt;
    b: Byte;
    fullset, gapset: set of TRange;
    bset: set of 0..250;
begin
  fullset := [-5..5];
  s := -3;
  write('neg3.in.fullset='); if s in fullset then writeln('TRUE') else writeln('FALSE');
  s := 3;
  write('pos3.in.fullset='); if s in fullset then writeln('TRUE') else writeln('FALSE');

  gapset := [];
  gapset := gapset + [-5..-1] + [1..5];
  s := 0;
  write('zero.in.gapset='); if s in gapset then writeln('TRUE') else writeln('FALSE');
  s := -3;
  write('neg3.in.gapset='); if s in gapset then writeln('TRUE') else writeln('FALSE');

  bset := [100..250];
  b := 200;
  write('byte200.in.bset='); if b in bset then writeln('TRUE') else writeln('FALSE');
  b := 50;
  write('byte50.in.bset='); if b in bset then writeln('TRUE') else writeln('FALSE');
end.
