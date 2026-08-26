(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

for

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: For
*)
