(*
RUN: %plang_ep -dump-ast %s
*)

program p; const N=10; type R = 1..N div 2; var x:R; begin x := 3 end.
