(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 10 111 222
*)

program p(output);
type row = array[5..10] of integer;
     rowalias = row;
var guard1: integer; x: rowalias; guard2: integer; i: integer;
begin
  guard1 := 111; guard2 := 222;
  for i := 5 to 10 do x[i] := i;
  writeln(x[5], ' ', x[10], ' ', guard1, ' ', guard2)
end.
