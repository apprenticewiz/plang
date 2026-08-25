(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[3.14]
CHECK-NEXT:[42]
CHECK-NEXT:[    3.14]
CHECK-NEXT:[  42]
*)

program p;
var x: real; n: integer;
begin
  x := 3.14159; n := 42;
  writeln('[', x:0:2, ']');
  writeln('[', n:0, ']');
  writeln('[', x:8:2, ']');
  writeln('[', n:4, ']')
end.
