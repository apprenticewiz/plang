(*
RUN: not %plang -dump-ast %s
*)

program p; var x : integer; begin x := true + false end.
