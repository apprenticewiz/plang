(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1024
*)

program p(output); var i: integer;
begin i := 2 pow 10; writeln(i) end.
