(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

{ (* not nested } x

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
*)
