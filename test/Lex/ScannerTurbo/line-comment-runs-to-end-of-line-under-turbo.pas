(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

x // this whole line is ignored under -std=turbo, including a lone / and a *)
y

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Identifier "y"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Eof
*)
