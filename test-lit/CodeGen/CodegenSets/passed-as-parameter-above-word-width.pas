(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:6
*)

program p;
var s: set of char;
function count(t: set of char): integer;
begin count := card(t) end;
begin
  s := ['a'..'e', 'x'];
  writeln(count(s))
end.
