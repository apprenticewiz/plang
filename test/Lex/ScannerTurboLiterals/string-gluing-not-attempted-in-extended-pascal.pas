(*
Same as string-gluing-not-attempted-outside-turbo.pas, under Extended
Pascal.
*)

(*
RUN: not %plang_ir -dump-tokens -std=iso10206 %s | FileCheck %s
*)

'AB'#65'CD'

(*
CHECK: [[P1:[0-9]+:[0-9]+]]: StringLit "AB"
CHECK-NEXT: [[P2:[0-9]+:[0-9]+]]: IntLit "65"
CHECK-NEXT: [[P3:[0-9]+:[0-9]+]]: StringLit "CD"
*)
