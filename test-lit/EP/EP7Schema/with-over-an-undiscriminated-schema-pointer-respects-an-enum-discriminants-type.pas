(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

program p;
type Color = (Red, Green, Blue);
     Box(c: Color) = record x: integer end;
var b: ^Box;
begin
  new(b, Green);
  with b^ do
    if c = Green then writeln(ord(c):1)
end.
