(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

'What the #&*%!'

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "What the #&*%!"
*)
