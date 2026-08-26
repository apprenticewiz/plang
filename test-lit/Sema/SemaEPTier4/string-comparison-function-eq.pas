(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s, t: string(20); b: boolean; begin b := EQ(s, t) end.
