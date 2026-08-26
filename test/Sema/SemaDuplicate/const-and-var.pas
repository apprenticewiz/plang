(*
RUN: %plang -dump-ast %s 2> %t.err; true
RUN: FileCheck %s < %t.err
*)

program p; const n = 1; var n : integer; begin end.

(*
CHECK: n
*)
