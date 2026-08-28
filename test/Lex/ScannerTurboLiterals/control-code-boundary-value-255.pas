(*
255 (0xFF) is the top of Char's 0..255 range (Sema.cpp's `maxchar`) and must
be accepted, not rejected as one past the end -- scanControlCodeFragment
bails only once Value EXCEEDS 255.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

#255

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit
CHECK-NOT: error
*)
