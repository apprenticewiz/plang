(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; label done; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (label done)
CHECK-NEXT:  (compound))
*)
