(*
ShortInt (Turbo's signed 8-bit sized-integer rung, TypeContext::getInt)
shares its i8 LLVM representation with Char/Boolean, both of which are
genuinely non-negative -- so CGAssign::emitAssignValue's plain-assignment
widening (the generic dstTy-is-wider-than-rhs's-own-type branch) used to
zero-extend unconditionally, on the pre-ladder assumption that i8 always
meant one of those two.  A negative ShortInt assigned into a wider
destination read its own bit pattern as a large positive number instead:
`s := -5; i := s` printed i=251, not -5 (issue #177).  Fixed by consulting
the source expression's own Sema-resolved signedness (exprIsSigned,
OrdinalSignedness.h) instead of guessing from the i8 width alone.

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:i=-5
CHECK-NEXT:l=-5
*)

program p;
var
  s: ShortInt;
  i: Integer;
  l: LongInt;
begin
  s := -5;
  i := s;
  writeln('i=', i);
  l := s;
  writeln('l=', l)
end.
