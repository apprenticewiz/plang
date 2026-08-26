(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p;
var x: integer;
var y: type of x;
begin x := 42; y := x; writeln(y) end.
