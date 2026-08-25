(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:q
*)

program p;
procedure q; begin writeln('q') end;
begin q end.
