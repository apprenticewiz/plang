(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:12
*)

program p;
function double(protected n: integer): integer;
begin double := n * 2 end;
begin writeln(double(6)) end.
