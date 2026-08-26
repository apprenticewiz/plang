(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:0
CHECK-NEXT:42
CHECK-NEXT:0
CHECK-NEXT:0
CHECK-NEXT:0
*)

program p;
type Row = array[1..5] of integer;
var r: Row;
    i: integer;
begin
  r := Row[2: 42; otherwise: 0];
  for i := 1 to 5 do writeln(r[i])
end.
