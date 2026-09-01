(*
256 is one past Char's 0..255 range.
*)

(*
RUN: not %plang_ir -dump-tokens -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

#256

(*
CHECK: control-character code '256' is out of range
*)
