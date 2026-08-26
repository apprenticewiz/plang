(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

mod

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Mod
*)
