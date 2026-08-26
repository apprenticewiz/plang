(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

then

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Then
*)
