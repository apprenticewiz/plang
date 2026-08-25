(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:45 1133 13
*)

program p(output);
type idx = 1..3;
var b: array[boolean] of integer;
    n: array[idx] of integer;
    e: array[(lo, mid, hi)] of integer;
begin
  b[false] := 4; b[true] := 5;
  n[1] := 11; n[3] := 33;
  e[lo] := 1; e[hi] := 3;
  writeln(b[false], b[true], ' ', n[1], n[3], ' ', e[lo], e[hi])
end.
