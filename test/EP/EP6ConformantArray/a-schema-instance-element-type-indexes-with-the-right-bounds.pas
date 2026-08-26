(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0 99 0
*)

program p(output);
type vec(n: integer) = array[1..n] of integer;
     row = vec(3);
procedure fill(var a: array[lo..hi: integer] of row);
begin a[lo][2] := 99 end;
var m: array[1..2] of row; i, j: integer;
begin
  for i := 1 to 2 do for j := 1 to 3 do m[i][j] := 0;
  fill(m);
  writeln(m[1][1]:1, ' ', m[1][2]:1, ' ', m[1][3]:1)
end.
