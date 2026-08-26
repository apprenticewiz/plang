(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

integer

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Integer
*)
