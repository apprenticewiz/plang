(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; begin ptr^ := 99 end.

(*
CHECK:(program p
CHECK-NEXT:  (compound
CHECK-NEXT:    (assign (deref ptr) 99)))
*)
