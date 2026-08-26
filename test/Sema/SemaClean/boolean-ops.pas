(*
RUN: %plang -dump-ast %s
*)

program p; var x, y : integer; b : boolean;
begin b := (x > 0) and (y > 0) end.
