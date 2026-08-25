(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:999
*)

program p(output);
function abs(x: integer): integer;
begin abs := 999 end;
begin writeln(abs(-3)) end.
