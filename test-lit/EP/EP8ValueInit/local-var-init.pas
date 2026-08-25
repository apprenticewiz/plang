(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7
*)

program p;
function add(a, b: integer): integer;
var result: integer value 0;
begin
  result := a + b;
  add := result
end;
begin
  writeln(add(3, 4))
end.
