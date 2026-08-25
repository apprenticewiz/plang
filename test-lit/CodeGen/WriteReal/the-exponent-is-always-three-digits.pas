(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1.00000000000000e+308
CHECK-NEXT: 1.00000000000000e-010
*)

program p(output);
begin writeln(1.0e308); writeln(1.0e-10) end.
