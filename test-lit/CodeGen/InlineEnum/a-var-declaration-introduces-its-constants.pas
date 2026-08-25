(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:2
*)

program p(output); var e: (red, green, blue);
begin e := blue; writeln(ord(e)) end.
