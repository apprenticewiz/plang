(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

set

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Set
*)
