(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 6 7 105 106 107 
*)

program p;
type m = array[0..1, 5..7] of integer;
var a: m;
    i, j: integer;
procedure fill(var x: array[l1..h1: integer;
                            l2..h2: integer] of integer);
var r, s: integer;
begin
  for r := l1 to h1 do
    for s := l2 to h2 do
      x[r][s] := r * 100 + s
end;
begin
  fill(a);
  for i := 0 to 1 do
    for j := 5 to 7 do write(a[i, j], ' ');
  writeln
end.
