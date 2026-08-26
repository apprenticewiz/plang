(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

until

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Until
*)
