(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:a X
CHECK-NEXT:b 9
CHECK-NEXT:a X
*)

program p;
procedure a; type t = char; var v: t;
begin v := 'X'; writeln('a ', v) end;
procedure b; type t = integer; var v: t;
begin v := 9; writeln('b ', v) end;
begin a; b; a end.
