(*
RUN: %plang_ep -dump-ast %s
*)

program p; const N=5; type R = 1..N; var x:R; begin x := 3 end.
