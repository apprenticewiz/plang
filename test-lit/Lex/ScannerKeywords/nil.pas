(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

nil

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Nil
*)
