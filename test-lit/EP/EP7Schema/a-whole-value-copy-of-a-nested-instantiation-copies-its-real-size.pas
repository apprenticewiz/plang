(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4 36 77 2
*)

program p(output);
type ent(cap: integer) = record a: array[1..cap] of integer; id: integer end;
     t(n: integer) = record e: ent(n); tail: integer end;
var q, r: ^t; i: integer;
begin new(q, 9); new(r, 9); q^.tail := 1; r^.tail := 2;
  for i := 1 to 9 do q^.e.a[i] := i*4;
  q^.e.id := 77; r^.e := q^.e;
  writeln(r^.e.a[1]:1,' ',r^.e.a[9]:1,' ',r^.e.id:1,' ',r^.tail:1) end.
