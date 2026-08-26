(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
CHECK-NEXT:20
CHECK-NEXT:30
CHECK-NEXT:40
CHECK-NEXT:50
*)

program p;
type Row = array[1..5] of integer;
var r: Row;
    i: integer;
begin
  r := Row[1: 10; 2: 20; 3: 30; 4: 40; 5: 50];
  for i := 1 to 5 do writeln(r[i])
end.
