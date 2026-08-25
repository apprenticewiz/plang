(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2 2 200
*)

program p(output);
type k = integer value 2;
     big = array [1..100] of k;
var h: big; i, sum: integer;
begin sum := 0; for i := 1 to 100 do sum := sum + h[i];
  writeln(h[1], ' ', h[100], ' ', sum) end.
