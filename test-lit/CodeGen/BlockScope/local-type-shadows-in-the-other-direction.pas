(*
RUN: %plang %s -o %t
RUN: %run %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:outer 7
CHECK-NEXT:inner Q
*)

program p;
type t = integer;
var g: t;
procedure q; type t = char; var v: t;
begin v := 'Q'; writeln('inner ', v) end;
begin g := 7; writeln('outer ', g); q end.
