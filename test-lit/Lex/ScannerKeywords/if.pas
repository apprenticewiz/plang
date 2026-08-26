(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

if

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: If
*)
