(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: set of 1..8; n:integer; begin n := card(s) end.
