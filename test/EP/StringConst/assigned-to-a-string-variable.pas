(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[hello] 5
*)

program p;
const greeting = 'hello';
var s: string(10);
begin s := greeting; writeln('[', s, '] ', length(s)) end.
