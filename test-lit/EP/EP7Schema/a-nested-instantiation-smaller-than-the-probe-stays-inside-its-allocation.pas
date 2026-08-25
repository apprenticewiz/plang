(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:y=5 tag=77 a2=3
*)

program p(output);
type inner(k: integer) = record a: array[1..10-k] of integer; tag: integer end;
     outer(m: integer) = record x: inner(m); y: integer end;
var q: ^outer;
begin new(q, 8); q^.x.tag := 77; q^.y := 5; q^.x.a[2] := 3;
  writeln('y=', q^.y:1, ' tag=', q^.x.tag:1, ' a2=', q^.x.a[2]:1) end.
