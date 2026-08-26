(*
RUN: not %plang_ep -dump-ast %s
*)

program p; var i:integer; s: set of 1..8; begin s := i >< s end.
