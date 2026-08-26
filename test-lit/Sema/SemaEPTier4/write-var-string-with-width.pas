(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(20); begin s := 'hi'; write(s:10) end.
