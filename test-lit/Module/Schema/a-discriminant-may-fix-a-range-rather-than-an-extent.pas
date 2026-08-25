(*
RUN: %plang -std=iso10206 %s -o %t
RUN: not %run %t > %t.out 2> %t.err
RUN: FileCheck --check-prefix=ERR %s < %t.err
RUN: FileCheck --check-prefix=ERR-ABSENT %s < %t.err
*)

(*
ERR: 1..100
ERR-ABSENT-NOT: outside the range
*)

program p(output);
type box(n: integer) = record k: 1..n; m: integer end;
var q: ^box;
begin
  new(q, 100);
  q^.k := 50; q^.m := 7;
  writeln('k=', q^.k:1, ' m=', q^.m:1);
  q^.k := 200;
  writeln('not reached')
end.
