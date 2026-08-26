(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

and

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: And
*)
