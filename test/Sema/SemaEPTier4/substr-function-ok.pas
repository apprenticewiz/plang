(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s, u: string(20); begin u := substr(s, 2, 4) end.
