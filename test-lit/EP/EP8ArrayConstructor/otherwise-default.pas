(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:99
CHECK-NEXT:99
CHECK-NEXT:99
CHECK-NEXT:99
*)

program p;
type Row = array[1..4] of integer;
var r: Row;
    i: integer;
begin
  r := Row[otherwise: 99];
  for i := 1 to 4 do writeln(r[i])
end.
