(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:A
CHECK-NEXT:B
CHECK-NEXT:C
*)

program p;
var a: array [1..3] of char;
begin
  a[1] := 'A'; a[2] := 'B'; a[3] := 'C';
  writeln(a[1]); writeln(a[2]); writeln(a[3])
end.
