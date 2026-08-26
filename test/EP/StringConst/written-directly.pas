(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello]
*)

program p;
const greeting = 'hello';
begin writeln('[', greeting, ']') end.
