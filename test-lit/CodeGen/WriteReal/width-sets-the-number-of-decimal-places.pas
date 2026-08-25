(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1.000000000000e+000
CHECK-NEXT: 1.0000e+000
CHECK-NEXT: 1.0e+000
*)

program p(output);
begin writeln(1.0:20); writeln(1.0:12); writeln(1.0:9) end.
