(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:42
*)

program p(output);
var odd: integer;
begin odd := 42; writeln(odd) end.
