(*
RUN: %plang -fno-range-checks %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:111 222
*)

program p(output);
type row = array[5..10] of integer;
     rowalias = row;
var guard1: integer; x: rowalias; guard2: integer; i: integer;
begin
  guard1 := 111; guard2 := 222;
  for i := 5 to 10 do x[i] := i;
  writeln(guard1, ' ', guard2)
end.
