(*
RUN: %plang_ep -dump-ast %s
*)

program p; var b:boolean; begin b := true and_then false end.
