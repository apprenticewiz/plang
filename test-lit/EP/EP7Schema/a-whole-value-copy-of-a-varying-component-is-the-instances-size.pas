(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a3=111 k=2
*)

program p(output);
type r(lo: integer) = record a: array[lo..3] of integer; k: integer end;
var p1, q1: ^r;
begin
  new(p1, 3); new(q1, 3);
  p1^.a[3] := 111; p1^.k := 1;
  q1^.a[3] := 222; q1^.k := 2;
  q1^.a := p1^.a;
  writeln('a3=', q1^.a[3]:1, ' k=', q1^.k:1)
end.
