(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:10 20 20 0 
*)

program p;
type row = array[1..4] of integer;
const v = row[1: 10; 2..3: 20; otherwise 0];
var i: integer;
begin
  for i := 1 to 4 do write(v[i], ' ');
  writeln
end.
