(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1.0e+000
*)

program p(output); begin writeln(1.0:1) end.
