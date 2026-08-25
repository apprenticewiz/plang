(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

program p(output);
procedure q; var e: (x, y); begin e := y; writeln(ord(e)) end;
begin q end.
