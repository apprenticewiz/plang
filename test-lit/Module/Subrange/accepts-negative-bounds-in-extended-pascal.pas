(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-1
*)

program p;
type r = -1..10;
var x: r;
begin x := -1; writeln(x) end.
