(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 1.5
*)

program p;
var n: integer value 7; r: real value 1.5;
begin writeln(n, ' ', r:0:1) end.
