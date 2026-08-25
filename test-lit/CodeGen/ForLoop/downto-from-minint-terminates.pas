(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

program p;
var i, n: integer;
begin n := 0; for i := -maxint + 1 downto -maxint do n := n + 1;
 writeln(n) end.
