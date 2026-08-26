(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[ 0.17  6]
*)

program p;
var S: string(20);
begin writestr(S, 0.168:5:2, 6:3); writeln('[', S, ']') end.
