(*
RUN: not %plang -dump-ast %s
*)

program p; var x : integer; begin x := 4 / 2 end.
