(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:8.0
*)

program p(output); var r: real;
begin r := 2 ** 3; writeln(r:0:1) end.
