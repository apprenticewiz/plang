(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
type Digit = 1..9;
     Box(d: Digit) = record x: integer end;
var b: Box(7);
begin b.x := 42; writeln(b.x:1) end.
