(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:   3.142
CHECK-NEXT:2.5
*)

program p(output);
begin writeln(3.14159:8:3); writeln(2.5:1:1) end.
