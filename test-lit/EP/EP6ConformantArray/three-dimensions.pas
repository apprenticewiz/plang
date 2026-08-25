(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1332
*)

program p;
type m = array[1..2, 1..2, 1..2] of integer;
var a: m;
    i, j, k: integer;
function total(var x: array[a1..b1: integer;
                            a2..b2: integer;
                            a3..b3: integer] of integer): integer;
var r, s, t, n: integer;
begin
  n := 0;
  for r := a1 to b1 do
    for s := a2 to b2 do
      for t := a3 to b3 do n := n + x[r, s, t];
  total := n
end;
begin
  for i := 1 to 2 do for j := 1 to 2 do for k := 1 to 2 do
    a[i, j, k] := i * 100 + j * 10 + k;
  writeln(total(a))
end.
