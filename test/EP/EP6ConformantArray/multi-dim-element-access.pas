(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:102
*)

program p;
type m = array[1..2, 1..3] of integer;
var a: m;
    i, j: integer;
function total(var x: array[l1..h1: integer;
                            l2..h2: integer] of integer): integer;
var r, s, t: integer;
begin
  t := 0;
  for r := l1 to h1 do
    for s := l2 to h2 do
      t := t + x[r, s];
  total := t
end;
begin
  for i := 1 to 2 do for j := 1 to 3 do a[i, j] := i * 10 + j;
  writeln(total(a))
end.
