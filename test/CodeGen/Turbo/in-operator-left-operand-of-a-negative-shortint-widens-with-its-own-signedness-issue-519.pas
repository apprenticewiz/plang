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
var s: ShortInt;
    b: Byte;
    bset: set of 0..250;
begin
  { Issue #692: a NAMED `set of -5..5` base type is refused outright under
    -std=turbo (a real TP7/fpc -Mtp restriction: a Turbo set base's own
    ordinals must be 0..255) -- see
    test/Sema/SemaTurboBoolReal/negative-based-set-is-refused-under-turbo.pas
    -- so this test's own negative-range coverage has to go through an
    UNTYPED set CONSTRUCTOR, `[-5..5]`, instead of a declared `set of
    TRange` variable the way it originally did.  literalSetWindow
    (SemaType.cpp), not checkSetBaseRange, derives an untyped constructor's
    window from its own elements and is not gated the same way -- confirmed
    against real `fpc -Mtp`, which accepts `s in [-5..5]` for a ShortInt s
    outright (only warning, not erroring, on the negative literal bound).
    Exercises the exact same CGBinaryOps' TokenKind::In arm this test was
    always about. }
  s := -3;
  write('neg3.in.fullset='); if s in [-5..5] then writeln('TRUE') else writeln('FALSE');
  s := 3;
  write('pos3.in.fullset='); if s in [-5..5] then writeln('TRUE') else writeln('FALSE');

  s := 0;
  write('zero.in.gapset='); if s in [-5..-1, 1..5] then writeln('TRUE') else writeln('FALSE');
  s := -3;
  write('neg3.in.gapset='); if s in [-5..-1, 1..5] then writeln('TRUE') else writeln('FALSE');

  bset := [100..250];
  b := 200;
  write('byte200.in.bset='); if b in bset then writeln('TRUE') else writeln('FALSE');
  b := 50;
  write('byte50.in.bset='); if b in bset then writeln('TRUE') else writeln('FALSE');
end.
