(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:14
*)

program p;
function double(n: integer) = result : integer;
begin result := n * 2 end;
begin writeln(double(7)) end.
