(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:-3 -2 -1 0 1 
*)

program p;
var s: set of -5..10; i: integer;
begin
  s := [-3 .. 1];
  for i in s do write(i, ' ');
  writeln
end.
