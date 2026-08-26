(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[]
*)

program p;
var t: string(20);
begin t := 'abc'; writeln('[', substr(t, 2, 0), ']') end.
