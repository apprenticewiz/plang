(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
CHECK-NEXT:7
CHECK-NEXT:7
CHECK-NEXT:9
CHECK-NEXT:9
*)

program p;
type Row = array[1..5] of integer;
var r: Row;
    i: integer;
begin
  r := Row[1..3: 7; 4..5: 9];
  for i := 1 to 5 do writeln(r[i])
end.
