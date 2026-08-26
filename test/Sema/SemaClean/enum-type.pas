(*
RUN: %plang -dump-ast %s
*)

program p;
type Color = (red, green, blue);
var c : Color;
begin c := red end.
