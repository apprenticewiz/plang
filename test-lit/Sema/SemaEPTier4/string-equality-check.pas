(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(10); b: boolean; begin b := s = 'hello' end.
