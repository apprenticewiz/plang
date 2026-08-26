(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a2=20 k=77 tail=99
*)

program p(output);
type inner(m: integer) = record a: array[1..m] of integer; k: integer end;
     outer(n: integer) = record x: inner(n); tail: integer end;
var q: ^outer;
begin new(q, 4); q^.x.a[2] := 20; q^.x.k := 77; q^.tail := 99;
  writeln('a2=', q^.x.a[2]:1, ' k=', q^.x.k:1, ' tail=', q^.tail:1) end.
