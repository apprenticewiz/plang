(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:5 10 15 
*)

program p;
type row = array[1..3] of integer;
var f: file of row;
    r: row;
    i: integer;
begin
  rewrite(f);
  for i := 1 to 3 do r[i] := i * 5;
  write(f, r);
  reset(f);
  read(f, r);
  for i := 1 to 3 do write(r[i], ' ');
  writeln
end.
