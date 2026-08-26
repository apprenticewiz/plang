(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:bcd abc c gh
*)

program p;
var t: string(20);
begin t := 'abcdefgh';
 writeln(substr(t,2,3), ' ', substr(t,1,3), ' ', substr(t,3,1),
 ' ', substr(t,7,2)) end.
