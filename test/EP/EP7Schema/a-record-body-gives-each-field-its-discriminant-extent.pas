(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2 0.5 2.5
*)

program p(output);
type poly(n: integer) = record
  deg: integer;
  c: array[0..n] of real
end;
var q: poly(2); i: integer;
begin
  q.deg := 2;
  for i := 0 to 2 do q.c[i] := i + 0.5;
  writeln(q.deg:0, ' ', q.c[0]:0:1, ' ', q.c[2]:0:1)
end.
