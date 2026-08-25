(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK: 1 2 3 4 5
*)

program p(output);
var i: integer;
begin
  i := 5;
  for i := 1 to i do write(i:2);
  writeln
end.
