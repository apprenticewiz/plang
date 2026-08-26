(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:11 3
*)

program p;
type digits = set of 0..9;
var a: array[1..2, 1..2] of integer;
    s: digits;
begin
  a[1, 1] := 4;
  a[2, 2] := 7;
  s := digits[1, 3, 5];
  writeln(a[1, 1] + a[2, 2], ' ', card(s))
end.
