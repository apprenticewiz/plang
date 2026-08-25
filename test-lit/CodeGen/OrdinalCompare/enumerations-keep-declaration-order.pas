(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:true false
*)

program p;
type col = (red, green, blue);
var a, b: col;
begin a := red; b := blue; writeln(a < b, ' ', b < a) end.
