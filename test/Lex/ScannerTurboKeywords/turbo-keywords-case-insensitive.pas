(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

ASM Constructor DESTRUCTOR Object Uses XOR

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: Asm
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: Constructor
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: Destructor
CHECK-NEXT: [[P4:[0-9]+:[0-9]+]]: Object
CHECK-NEXT: [[P5:[0-9]+:[0-9]+]]: Uses
CHECK-NEXT: [[P6:[0-9]+:[0-9]+]]: Xor
*)
