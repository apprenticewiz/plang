(*
RUN: %plang -dump-ast %s
*)

program p; type Count = 0..99; var n:Count; begin n := 7 end.
