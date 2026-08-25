(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bcd
*)

program p;
var t: string(20);
begin t := 'abcdefgh'; writeln(t[2..4]) end.
