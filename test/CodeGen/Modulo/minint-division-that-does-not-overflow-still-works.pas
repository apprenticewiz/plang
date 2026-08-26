(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-9223372036854775808 1 -4611686018427387904 4611686018427387903
*)

program p;
var minint, maxint: integer;
begin
  minint := -9223372036854775807; minint := minint - 1;
  maxint := 9223372036854775807;
  writeln(minint div 1, ' ', minint div minint, ' ', minint div 2, ' ',
          maxint div 2)
end.
