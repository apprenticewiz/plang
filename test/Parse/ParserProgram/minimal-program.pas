(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (compound))
*)
