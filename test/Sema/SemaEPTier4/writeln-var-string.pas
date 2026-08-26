(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(20); begin s := 'hi'; writeln(s) end.
