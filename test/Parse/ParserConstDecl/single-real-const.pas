(*
RUN: %plang_ir -dump-parse-tree %s | FileCheck --strict-whitespace --match-full-lines %s
*)

program p; const d = 0.0625; begin end.

(*
CHECK:(program p
CHECK-NEXT:  (const d 0.0625)
CHECK-NEXT:  (compound))
*)
