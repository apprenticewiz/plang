(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:256
*)

program p;
var s: set of char;
begin
  s := [chr(0)..chr(255)];
  writeln(card(s))
end.
