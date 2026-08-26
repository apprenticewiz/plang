(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:defgh
*)

program p;
var t: string(20);
begin t := 'abcdefgh'; writeln(substr(t, 4)) end.
