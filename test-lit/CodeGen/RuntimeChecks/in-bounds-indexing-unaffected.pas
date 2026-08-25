(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:60
*)

program p;
var a: array[1..5] of integer; i: integer;
begin
  for i := 1 to 5 do a[i] := i * 10;
  writeln(a[1] + a[5])
end.
