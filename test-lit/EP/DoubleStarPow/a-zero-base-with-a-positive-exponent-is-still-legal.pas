(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0.0
*)

program p(output); begin writeln(0.0 ** 2.0:0:1) end.
