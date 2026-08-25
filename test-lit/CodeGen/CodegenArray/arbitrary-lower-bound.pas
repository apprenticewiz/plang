(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-2
CHECK-NEXT:-1
CHECK-NEXT:0
CHECK-NEXT:1
CHECK-NEXT:2
*)

program p;
var a: array [-2..2] of integer; i: integer;
begin
  for i := -2 to 2 do a[i] := i;
  for i := -2 to 2 do writeln(a[i])
end.
