(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3 true false
*)

program p;
begin
  writeln(card([-4, -2, 7]), ' ', -4 in [-4, -2, 7], ' ', -3 in [-4, -2, 7])
end.
