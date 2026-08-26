(*
RUN: %plang_ep -dump-ast %s
*)

program p; var s: string(10); c: char; begin c := 'x'; s := c end.
