(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 2 0 4
*)

program p;
begin writeln((-17) mod 5, ' ', 17 mod 5, ' ', (-15) mod 5,
 ' ', (-1) mod 5) end.
