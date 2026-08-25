(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9.0000
*)

program p;
type pr = ^real;
var p: pr;
begin new(p); p^ := 9; writeln(p^:0:4); dispose(p) end.
