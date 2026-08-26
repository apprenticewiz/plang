(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:9
*)

program p(output);
type Box(c: integer) = record x: integer end;
     MyBox = Box(5);
var b: MyBox;
begin b.x := 9; writeln(b.x:1) end.
