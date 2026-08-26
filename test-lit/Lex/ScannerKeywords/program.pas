(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

program

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Program
*)
