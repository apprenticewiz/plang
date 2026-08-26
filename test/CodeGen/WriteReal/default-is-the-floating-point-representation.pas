(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1.00000000000000e+000
CHECK-NEXT:-2.50000000000000e+000
CHECK-NEXT: 0.00000000000000e+000
*)

program p(output);
begin writeln(1.0); writeln(-2.5); writeln(0.0) end.
