(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s, u: string(40); begin u := s + 'world' end.
