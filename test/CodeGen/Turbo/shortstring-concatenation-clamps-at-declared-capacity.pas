(*
Turbo string[N] semantics item, concrete work 4: `+` concatenation clamps
at the DESTINATION's declared capacity (never above 255, a ShortString's
own ceiling -- see plang_sstr_concat/plang_sstr.cpp), silently, with no
error and no memory corruption.  Exercises ShortString+ShortString,
ShortString+literal and ShortString+Char, matching CGBinaryOps.cpp's own
ShortString concatenation block (a sibling of the EP one, never routed
through it -- see Sema::checkBinary's identical Plus-case split,
SemaExpr.cpp).

RUN: %plang -std=turbo %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

program p;
var
  a, b: string[5];
  c: string[3];
begin
  a := 'ab';
  b := 'cd';
  c := a + b;             { 'abcd', 4 chars, clamped to c's capacity 3 }
  writeln(c);
  a := a + 'XYZ12345';     { 'ab' + 8 chars, clamped to a's capacity 5 }
  writeln(a);
  b := 'z';
  b := b + 'Q';            { char operand on the right }
  writeln(b);
end.

(*
CHECK:abc
CHECK-NEXT:abXYZ
CHECK-NEXT:zQ
*)
