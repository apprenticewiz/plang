(*
RUN: not %plang -dump-ast %s
*)

program p; var x : integer; begin repeat x := 0 until 1 end.
