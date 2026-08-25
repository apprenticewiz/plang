(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:  3.00
CHECK-NEXT:  3.00
CHECK-NEXT:  3.50
*)

program p(output);
var n: integer;
procedure scale(x: real); begin writeln(x:6:2) end;
function half(x: real): real; begin half := x / 2 end;
begin n := 3; scale(3); scale(n); writeln(half(7):6:2) end.
