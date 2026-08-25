(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4052555153018976267
*)

program p(output); begin writeln(3 pow 39) end.
