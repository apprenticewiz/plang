(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9
*)

program p;
var s: set of 1..5;
var v, total: integer;
begin
  s := [1, 3, 5];
  total := 0;
  for v in s do total := total + v;
  writeln(total)
end.
