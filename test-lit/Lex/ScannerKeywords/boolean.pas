(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

boolean

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Boolean
*)
