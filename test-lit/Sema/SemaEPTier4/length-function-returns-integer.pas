(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(20); n: integer; begin n := length(s) end.
