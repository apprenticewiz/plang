(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5.0000
*)

program p;
type r = record x: real end;
var q: r;
begin q.x := 5; writeln(q.x:0:4) end.
