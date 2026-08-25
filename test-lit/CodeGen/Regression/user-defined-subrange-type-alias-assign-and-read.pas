(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:100
*)

program p;
type Pct = 0..100;
var a, b: Pct;
begin
  a := 40;
  b := 60;
  writeln(a + b)
end.
