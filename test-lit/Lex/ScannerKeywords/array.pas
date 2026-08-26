(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

array

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Array
*)
