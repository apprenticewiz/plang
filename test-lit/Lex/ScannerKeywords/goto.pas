(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

goto

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Goto
*)
