(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-2..4: -2 -1 0 1 2 3 4
*)

program p;
type win(lo, hi: integer) = array[lo..2 * hi] of integer;
var w: win(-2, 2); i: integer;
procedure show(var v: win);
var j: integer;
begin
  write(v.lo, '..', 2 * v.hi, ':');
  for j := v.lo to 2 * v.hi do write(' ', v[j]);
  writeln
end;
begin
  for i := -2 to 4 do w[i] := i;
  show(w)
end.
