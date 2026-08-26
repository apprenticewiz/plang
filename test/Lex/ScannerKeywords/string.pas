(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

string

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: String
*)
