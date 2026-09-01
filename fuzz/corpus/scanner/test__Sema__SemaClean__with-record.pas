(*
RUN: %plang -dump-ast %s
*)

program p;
type Point = record x : integer end;
var pt : Point;
begin with pt do x := 1 end.
