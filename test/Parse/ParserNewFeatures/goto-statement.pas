(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; label 99; begin goto 99 end.

(*
CHECK:(program p
CHECK-NEXT:  (label 99)
CHECK-NEXT:  (compound
CHECK-NEXT:    (goto 99)))
*)
