(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4: 1 4 9 16
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var q: ^vec; k, i: integer;
begin
  k := 4;
  new(q, k);
  for i := 1 to q^.n do q^[i] := i * i;
  write(q^.n, ':');
  for i := 1 to q^.n do write(' ', q^[i]);
  writeln;
  dispose(q)
end.
