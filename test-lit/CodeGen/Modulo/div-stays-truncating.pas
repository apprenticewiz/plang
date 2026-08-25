(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-3 3
*)

program p;
begin writeln((-17) div 5, ' ', 17 div 5) end.
