(*
RUN: not %plang_ir -dump-parse-tree %s
*)

program p; var x : integer; begin while x > 0 x := x - 1 end.
