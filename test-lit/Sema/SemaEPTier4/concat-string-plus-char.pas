(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(10); c: char; r: string(11); begin r := s + c end.
