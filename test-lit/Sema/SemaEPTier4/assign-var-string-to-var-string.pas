(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s, t: string(20); begin s := t end.
