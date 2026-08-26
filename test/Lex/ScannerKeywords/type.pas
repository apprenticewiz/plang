(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

type

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Type
*)
