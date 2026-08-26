(*
RUN: not %plang_ir -dump-tokens %s | FileCheck %s
*)

my_var

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "my_var"
*)
