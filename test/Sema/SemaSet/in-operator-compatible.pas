(*
RUN: %plang -dump-ast %s
*)

program p; var x : integer; b : boolean;
begin b := x in [1, 2, 3] end.
