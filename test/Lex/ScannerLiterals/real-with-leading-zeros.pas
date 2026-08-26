(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

00000000000000000003.14159265

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: RealLit "00000000000000000003.14159265"
*)
