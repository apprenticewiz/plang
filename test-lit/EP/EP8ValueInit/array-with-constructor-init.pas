(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10
CHECK-NEXT:20
CHECK-NEXT:30
*)

program p;
type Row = array[1..3] of integer;
var r: Row value Row[1: 10; 2: 20; 3: 30];
    i: integer;
begin
  for i := 1 to 3 do writeln(r[i])
end.
