(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:3.5 7
*)

program p(output);
var pi: real; maxint: integer;
begin pi := 3.5; maxint := 7; writeln(pi:0:1, ' ', maxint) end.
