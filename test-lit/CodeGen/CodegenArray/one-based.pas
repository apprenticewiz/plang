(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
CHECK-NEXT:20
CHECK-NEXT:30
CHECK-NEXT:40
CHECK-NEXT:50
*)

program p;
var a: array [1..5] of integer; i: integer;
begin
  for i := 1 to 5 do a[i] := i * 10;
  for i := 1 to 5 do writeln(a[i])
end.
