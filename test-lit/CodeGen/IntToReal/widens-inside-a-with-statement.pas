(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.0000
*)

program p;
type r = record x: real end;
var q: r;
begin with q do x := 3; writeln(q.x:0:4) end.
