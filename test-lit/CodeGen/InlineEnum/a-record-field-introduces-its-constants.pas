(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:1
*)

program p(output); var r: record c: (red, green) end;
begin r.c := green; writeln(ord(r.c)) end.
