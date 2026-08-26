(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s, t: string(10); b: boolean; begin b := s < t end.
