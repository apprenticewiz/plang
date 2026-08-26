(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 14 21
*)

program p;
type vec(n: integer) = array[1..n] of integer;
var a, b: ^vec; i: integer;
begin
  new(a, 3); new(b, 3);
  for i := 1 to 3 do a^[i] := i * 7;
  b^ := a^;
  writeln(b^[1], ' ', b^[2], ' ', b^[3]);
  dispose(a); dispose(b)
end.
