(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:50
CHECK-NEXT:99
*)

program p;
var a: array [1..5] of integer; i: integer;
begin
  for i := 1 to 5 do a[i] := i * 10;
  i := 99;
  writeln(a[5]);
  writeln(i)
end.
