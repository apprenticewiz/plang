(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-5
*)

program p(output);
var v: -maxint..maxint;
begin v := -5; writeln(v:1) end.
