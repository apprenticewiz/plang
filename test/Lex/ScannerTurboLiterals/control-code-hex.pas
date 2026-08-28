(*
'#$XX' names the character by its hex ordinal instead of decimal.
0x41 = 65 = 'A', same character as control-code-decimal.pas's #65.
*)

(*
RUN: %plang_ir -dump-tokens -std=turbo %s | FileCheck %s
*)

#$41

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "A"
*)
