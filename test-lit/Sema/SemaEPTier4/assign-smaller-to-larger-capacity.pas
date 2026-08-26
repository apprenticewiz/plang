(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(5); t: string(20); begin t := s end.
