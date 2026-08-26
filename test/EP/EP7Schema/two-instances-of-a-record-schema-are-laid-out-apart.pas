(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2 2.0
CHECK-NEXT:5 105.0
*)

program p(output);
type poly(n: integer) = record
  deg: integer;
  c: array[0..n] of real
end;
var big: poly(5); small: poly(2); i: integer;
begin
  small.deg := 2; big.deg := 5;
  for i := 0 to 2 do small.c[i] := i;
  for i := 0 to 5 do big.c[i] := 100 + i;
  writeln(small.deg:0, ' ', small.c[2]:0:1);
  writeln(big.deg:0, ' ', big.c[5]:0:1)
end.
