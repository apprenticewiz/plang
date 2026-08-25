(*
RUN: %plang %s -o %t
RUN: %t | FileCheck --strict-whitespace --match-full-lines %s
*)

(*
CHECK:outer Z
CHECK-NEXT:inner 65
CHECK-NEXT:outer Z
*)

program p;
type t = char;
var g: t;
procedure q; type t = integer; var v: t;
begin v := 65; writeln('inner ', v) end;
begin g := 'Z'; writeln('outer ', g); q;
 writeln('outer ', g) end.
