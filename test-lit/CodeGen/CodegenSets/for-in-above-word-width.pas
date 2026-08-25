(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 70 200 
*)

program p;
var s: set of 0..255; i: integer;
begin
  s := [3, 70, 200];
  for i in s do write(i, ' ');
  writeln
end.
