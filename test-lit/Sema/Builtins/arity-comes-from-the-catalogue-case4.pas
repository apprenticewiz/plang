(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(8); begin s := substr('abc', 1, 2) end.
