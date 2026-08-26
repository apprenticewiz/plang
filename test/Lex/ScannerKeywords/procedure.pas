(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

procedure

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Procedure
*)
