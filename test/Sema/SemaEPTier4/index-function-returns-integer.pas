(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s, p: string(20); n: integer; begin n := index(s, p) end.
