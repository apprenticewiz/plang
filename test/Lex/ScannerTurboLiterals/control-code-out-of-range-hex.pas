(*
Same out-of-range check applies to the '#$..' hex spelling. 0x100 = 256.
*)

(*
RUN: not %plang_ir -dump-tokens -std=turbo %s 2> %t.err
RUN: FileCheck %s < %t.err
*)

#$100

(*
CHECK: control-character code '100' is out of range
*)
