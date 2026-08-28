(*
A single '/' is division under every dialect, including Turbo -- only a
DOUBLED '//' starts a line comment, and only under -std=turbo.

RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

x / y

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Identifier "x"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Divide
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Identifier "y"
*)
