(*
RUN: not %plang_ir -dump-parse-tree %s
*)

program p; var x : integer; begin if x > 0 x := 1 end.
