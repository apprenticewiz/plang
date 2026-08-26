(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

file

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: File
*)
