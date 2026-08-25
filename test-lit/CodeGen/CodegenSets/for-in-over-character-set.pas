(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:abz
*)

program p;
var s: set of char; c: char;
begin
  s := ['a', 'b', 'z'];
  for c in s do write(c);
  writeln
end.
