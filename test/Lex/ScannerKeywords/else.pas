(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

else

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Else
*)
