(*
RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

my_var

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "my_var"
*)
