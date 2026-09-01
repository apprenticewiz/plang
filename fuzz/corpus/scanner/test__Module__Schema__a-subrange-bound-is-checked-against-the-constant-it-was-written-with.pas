(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:k=50 m=3
*)

program p(output);
const lim = 100;
type box(n: integer) = record k: 1..n*lim; m: integer end;
var q: ^box;
procedure touch;
var lim: integer;
begin
  lim := 3;
  q^.k := 50; q^.m := lim
end;
begin
  new(q, 2);
  touch;
  writeln('k=', q^.k:1, ' m=', q^.m:1)
end.
