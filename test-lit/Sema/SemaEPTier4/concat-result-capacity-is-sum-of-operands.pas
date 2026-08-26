(*
RUN: %plang_ep -dump-ast %s
*)

program p; var a, b: string(10); u: string(20); begin u := a + b end.
