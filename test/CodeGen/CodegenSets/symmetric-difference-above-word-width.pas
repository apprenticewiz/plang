(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 250 
*)

program p;
var a, b, c: set of 0..255; i: integer;
begin
  a := [1, 100]; b := [100, 250];
  c := a >< b;
  for i in c do write(i, ' ');
  writeln
end.
