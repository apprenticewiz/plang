(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1 5
*)

program p(output);
type Color = (Red, Green, Blue);
     Box(c: Color) = record x: integer end;
var b: Box(Green);
begin b.x := 5; writeln(ord(Green):1, ' ', b.x:1) end.
