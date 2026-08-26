(*
RUN: %plang_ir -dump-tokens %s | FileCheck %s
*)

function

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Function
*)
