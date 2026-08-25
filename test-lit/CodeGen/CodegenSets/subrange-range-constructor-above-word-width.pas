(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:22 true false
*)

program p;
var s: set of 0..255; i, n: integer;
begin
  s := [10..20, 200..210];
  n := 0;
  for i := 0 to 255 do if i in s then n := n + 1;
  writeln(n, ' ', 205 in s, ' ', 100 in s)
end.
