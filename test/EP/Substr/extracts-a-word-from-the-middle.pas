(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:World
*)

program p;
var t: string(20);
begin t := 'Hello World'; writeln(substr(t, 7, 5)) end.
