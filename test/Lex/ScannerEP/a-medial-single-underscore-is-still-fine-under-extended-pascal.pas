(*
RUN: %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

foo_bar

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "foo_bar"
*)
