(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

record

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Record
*)
