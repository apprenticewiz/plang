(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

3.141592653589793238462643

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: RealLit "3.141592653589793238462643"
*)
