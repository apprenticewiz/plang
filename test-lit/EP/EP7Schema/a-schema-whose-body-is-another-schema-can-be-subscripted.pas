(*
RUN: %plang -std=iso10206 -frange-checks %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 20 5 20
*)

program p(output);
type vec(n: integer) = array[1..n] of integer;
     v2(n: integer)  = vec(n);
var x: v2(4); y: vec(4); i: integer;
begin
  for i := 1 to 4 do y[i] := i * 5;
  for i := 1 to 4 do x[i] := i * 5;
  writeln(y[1]:1, ' ', y[4]:1, ' ', x[1]:1, ' ', x[4]:1)
end.
