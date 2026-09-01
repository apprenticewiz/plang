(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

do

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Do
*)
