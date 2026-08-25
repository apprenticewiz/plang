(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello there]
*)

program p;
const greeting = 'hello';
var s: string(20);
begin s := greeting + ' there'; writeln('[', s, ']') end.
