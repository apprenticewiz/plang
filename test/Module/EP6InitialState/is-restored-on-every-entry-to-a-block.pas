(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6 6 
*)

program p(output);
type counter = integer value 5;
procedure bump;
var n: counter;
begin n := n + 1; write(n, ' ') end;
begin bump; bump; writeln end.
