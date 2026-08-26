(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

downto

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Downto
*)
