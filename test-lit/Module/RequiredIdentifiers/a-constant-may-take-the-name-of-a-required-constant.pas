(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.5
*)

program p(output);
const pi = 3.5;
begin writeln(pi:0:1) end.
