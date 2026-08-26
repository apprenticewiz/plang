(*
RUN: %plang_ep -dump-ast %s
*)

program p; var i:integer; begin i := pred(i, 3) end.
