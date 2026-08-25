(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:[abcd] k=2
*)

program p(output);
type r(n: integer) = record a: array[1..3] of string(n); k: integer end;
var p1, q1: ^r;
begin
  new(p1, 4); new(q1, 4);
  p1^.a[2] := 'abcd'; p1^.k := 1; q1^.k := 2;
  q1^.a := p1^.a;
  writeln('[', q1^.a[2], '] k=', q1^.k:1)
end.
