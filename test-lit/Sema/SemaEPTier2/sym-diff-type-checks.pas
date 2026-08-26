(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s,t,u: set of 1..8; begin u := s >< t end.
