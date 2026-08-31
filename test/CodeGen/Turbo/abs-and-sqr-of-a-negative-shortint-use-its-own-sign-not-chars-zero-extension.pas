(*
CGFuncCall's Abs/Sqr integer path called ToI64 with no signedness
argument, the same pre-ladder "guess zero-extend for any i8" fallback
CGAssign's plain-assignment path used to fall into (see shortint-
assigned-to-a-wider-signed-integer-sign-extends-issue-177.pas): a negative
ShortInt argument's zero-extended bit pattern is a large positive number,
so Abs(-5) computed abs(251) = 251 instead of 5, and Sqr(-5) computed
251*251 = 63001 instead of 25 (issue #177's sibling audit).  Fixed by
consulting the argument's own Sema-resolved signedness (exprIsSigned,
OrdinalSignedness.h).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abs=5
CHECK-NEXT:sqr=25
*)

program p;
var s: ShortInt;
begin
  s := -5;
  writeln('abs=', abs(s));
  writeln('sqr=', sqr(s))
end.
