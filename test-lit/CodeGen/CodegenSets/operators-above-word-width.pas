(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4 2 1
CHECK-NEXT:true false false
*)

program p;
var a, b: set of 0..255;
begin
  a := [64, 100, 200]; b := [100, 200, 255];
  writeln(card(a + b), ' ', card(a * b), ' ', card(a - b));
  writeln(64 in (a + b), ' ', 64 in (a * b), ' ', 255 in (a - b))
end.
