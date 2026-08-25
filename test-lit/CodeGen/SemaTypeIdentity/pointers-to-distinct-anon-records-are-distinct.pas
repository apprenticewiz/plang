(*
RUN: not %plang %s -o %t
*)

program p;
type p1 = ^record a: integer end;
     p2 = ^record b, c, d: integer end;
var x: p1; y: p2;
begin new(x); new(y); x := y end.
