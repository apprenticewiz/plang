(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-1.0e-030
*)

program p(output); begin writeln(-1.0e-30:9) end.
