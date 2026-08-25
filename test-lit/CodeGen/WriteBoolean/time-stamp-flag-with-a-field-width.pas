(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[   true]
*)

program p;
var t: TimeStamp;
begin GetTimeStamp(t); writeln('[', t.DateValid:7, ']') end.
