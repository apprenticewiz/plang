(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5
*)

program p;
type T = bindable integer;
var x: T;
begin x := 5; writeln(x) end.
