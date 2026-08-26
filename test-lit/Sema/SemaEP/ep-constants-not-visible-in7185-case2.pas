(*
RUN: not %plang -dump-ast %s
*)

program p; var c: char; begin c := maxchar end.
