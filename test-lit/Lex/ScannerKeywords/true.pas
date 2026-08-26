(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

true

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: True
*)
