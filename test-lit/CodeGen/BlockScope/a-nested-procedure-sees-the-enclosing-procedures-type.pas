(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a 1
CHECK-NEXT:b 3
*)

program p;
procedure a; type t = integer; var v: t;
 procedure b; var w: t; begin w := 3; writeln('b ', w) end;
begin v := 1; writeln('a ', v); b end;
begin a end.
