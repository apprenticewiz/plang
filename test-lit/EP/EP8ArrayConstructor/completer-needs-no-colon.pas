(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1010 00 00 00 
*)

program p;
type row = array[1..4] of integer;
var a, b: row;
    i: integer;
begin
  a := row[1: 10; otherwise 0];
  b := row[1: 10; otherwise: 0];
  for i := 1 to 4 do write(a[i], b[i], ' ');
  writeln
end.
