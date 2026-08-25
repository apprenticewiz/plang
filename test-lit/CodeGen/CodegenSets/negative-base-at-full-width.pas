(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:256 true true
*)

program p;
var s: set of -256..-1;
begin
  s := [-256 .. -1];
  writeln(card(s), ' ', -256 in s, ' ', -1 in s)
end.
