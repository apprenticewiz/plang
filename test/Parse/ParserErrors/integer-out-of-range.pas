(*
RUN: not %plang_ir -dump-parse-tree %s
*)

program p; var x : integer; begin x := 10000000000000000000 end.
