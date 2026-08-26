(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; label 10, 20; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (label 10 20)
CHECK-NEXT:  (compound))
*)
