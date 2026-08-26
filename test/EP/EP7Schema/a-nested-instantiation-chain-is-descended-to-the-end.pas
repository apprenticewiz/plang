(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 2 3 4 5 6 7 t=1
*)

program p(output);
type A(k: integer) = array[1..k] of integer;
     B(n: integer) = A(n*2+1);
     C(n: integer) = record b: B(n); t: integer end;
var q: ^C; i: integer;
begin new(q,3); q^.t := 1;
  for i := 1 to 7 do q^.b[i] := i;
  for i := 1 to 7 do write(q^.b[i]:1,' '); writeln('t=',q^.t:1) end.
