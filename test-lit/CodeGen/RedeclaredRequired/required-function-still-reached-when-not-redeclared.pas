(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3
CHECK-NEXT:3
CHECK-NEXT:2
*)

program p(output);
begin writeln(abs(-3)); writeln(round(2.6)); writeln(trunc(2.6)) end.
