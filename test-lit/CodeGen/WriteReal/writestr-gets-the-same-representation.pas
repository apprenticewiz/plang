(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1.00000000000000e+000
*)

program p(output); var s: string(40);
begin writestr(s, 1.0); writeln(s) end.
