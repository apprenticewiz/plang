(*
RUN: %plang_ep -dump-ast %s
*)

program p; var b:boolean; begin b := false or_else true end.
