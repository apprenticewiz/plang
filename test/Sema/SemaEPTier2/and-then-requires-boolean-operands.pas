(*
RUN: not %plang_ep -dump-ast %s
*)

program p; var i:integer; b:boolean; begin b := i and_then true end.
