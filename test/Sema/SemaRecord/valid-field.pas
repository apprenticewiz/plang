(*
RUN: %plang -dump-ast %s
*)

program p;
type Point = record x, y : real end;
var pt : Point;
begin pt.x := 1.0 end.
