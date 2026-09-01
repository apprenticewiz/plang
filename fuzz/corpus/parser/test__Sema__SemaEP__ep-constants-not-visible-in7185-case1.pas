(*
RUN: not %plang -dump-ast %s
*)

program p; var r: real; begin r := minreal end.
