(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:q^[8]=8
*)

program p(output);
const n = 3;
type t(n: integer) = array[1..n] of integer;
var q: ^t; i: integer;
begin
  new(q, 8);
  for i := 1 to 8 do q^[i] := i;
  writeln('q^[8]=', q^[8]:1)
end.
