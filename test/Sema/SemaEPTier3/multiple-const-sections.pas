(*
RUN: %plang_ep -dump-ast %s
*)

program p; const A = 1; var x:integer; const B = 2; begin x := A + B end.
