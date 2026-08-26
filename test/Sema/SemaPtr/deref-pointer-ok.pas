(*
RUN: %plang -dump-ast %s
*)

program p; var p : ^integer; begin p^ := 5 end.
