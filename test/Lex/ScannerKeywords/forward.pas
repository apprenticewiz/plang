(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

forward

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Forward
*)
