(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:7 3
*)

program p(output);
var total: integer;
function f: integer;
begin f := 7 end;
begin total := 3; writeln(f, ' ', total) end.
