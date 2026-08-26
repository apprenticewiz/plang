(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

char

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Char
*)
