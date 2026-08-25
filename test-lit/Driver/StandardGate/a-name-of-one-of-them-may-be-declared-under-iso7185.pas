(*
RUN: %plang -std=iso7185 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:4 3
*)

program p;
var card, length: integer;
function trim(x: integer): integer; begin trim := x - 1 end;
begin card := 4; length := trim(card); writeln(card, ' ', length) end.
