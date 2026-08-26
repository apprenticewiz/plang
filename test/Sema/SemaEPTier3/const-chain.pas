(*
RUN: %plang_ep -dump-ast %s
*)

program p; const A=1; B=A+1; C=B+1; D=C+1; var i:integer; begin i := D end.
