(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

repeat

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Repeat
*)
