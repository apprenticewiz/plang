(*
CGControlFlow::emitCase widens the selector and every label bound to i64
through ToI64 with no signedness argument -- the same pre-ladder "guess
zero-extend for any i8" fallback CGAssign's plain-assignment path used to
fall into (see shortint-assigned-to-a-wider-signed-integer-sign-extends-
issue-177.pas).  A case selector of a negative-based Turbo subrange (TP7
ch.19's narrowestStorage very often picks an i8-signed, ShortInt-shaped
storage for exactly this kind of small negative-to-positive range) had its
own zero-extended bit pattern compared against equally-mismatched label
bounds, so the arm that should have matched silently did not (issue
#177's sibling audit).  Fixed by consulting each operand's own Sema-
resolved signedness (exprIsSigned, OrdinalSignedness.h).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:neg
CHECK-NEXT:zero
CHECK-NEXT:pos
*)

program p;
var sub: -5..5;

procedure classify;
begin
  case sub of
    -5..-1: writeln('neg');
    0:      writeln('zero');
    1..5:   writeln('pos');
  end
end;

begin
  sub := -3; classify;
  sub := 0;  classify;
  sub := 4;  classify
end.
