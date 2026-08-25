(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:empty
*)

program p;
var s: set of 1..10;
begin s := [5..1]; if s = [] then writeln('empty') else writeln('not empty') end.
