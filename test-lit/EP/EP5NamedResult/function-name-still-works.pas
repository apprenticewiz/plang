(*
RUN: %plang -std=iso10206 %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:15
*)

program p;
function triple(n: integer) = res : integer;
begin triple := n * 3 end;
begin writeln(triple(5)) end.
