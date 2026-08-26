(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(20); u: string(20); begin u := s[2..4] end.
