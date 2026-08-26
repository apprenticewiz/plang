(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0
*)

program p;
var s: set of 0..10;
var v, count: integer;
begin
  s := [];
  count := 0;
  for v in s do count := count + 1;
  writeln(count)
end.
